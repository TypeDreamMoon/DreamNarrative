// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeStageArbiter.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"

namespace
{
	void SetLightIntensity(ULightComponentBase* Light, float Intensity)
	{
		if (ULightComponent* Local = Cast<ULightComponent>(Light))
		{
			Local->SetIntensity(Intensity);
		}
		else if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Light))
		{
			Sky->SetIntensity(Intensity);
		}
	}
}

bool UDreamNarrativeStageArbiter::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor
		|| WorldType == EWorldType::GamePreview || WorldType == EWorldType::EditorPreview;
}

void UDreamNarrativeStageArbiter::Deinitialize()
{
	// Whatever is still held goes back as it was; the view goes with the world.
	TimeScales.Reset();
	ApplyTime();
	LightShares.Reset();
	ApplyLights();
	for (const FHudHold& Hold : Huds)
	{
		if (AHUD* Hud = Hold.Hud.Get()) { Hud->bShowHUD = Hold.bWasShown; }
	}
	Huds.Reset();
	Views.Reset();
	Super::Deinitialize();
}

// ---------------------------------------------------------------- time

void UDreamNarrativeStageArbiter::SetTimeDilation(UDreamNarrativeStageDirector* Director, float Scale)
{
	const UWorld* World = GetWorld();
	const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	if (!Director || !Settings)
	{
		return;
	}
	if (FMath::IsNearlyEqual(Scale, 1.f))
	{
		ReleaseTimeDilation(Director);
		return;
	}
	if (!bTimeHeld)
	{
		OriginalTime = Settings->TimeDilation;
		bTimeHeld = true;
	}
	TimeScales.Add(Director, Scale);
	ApplyTime();
}

void UDreamNarrativeStageArbiter::ReleaseTimeDilation(UDreamNarrativeStageDirector* Director)
{
	if (TimeScales.Remove(Director) > 0)
	{
		ApplyTime();
	}
}

void UDreamNarrativeStageArbiter::ApplyTime()
{
	if (!bTimeHeld)
	{
		return;
	}
	float Combined = OriginalTime;
	for (const TPair<FDirectorRef, float>& Pair : TimeScales)
	{
		Combined *= Pair.Value;
	}
	if (const UWorld* World = GetWorld())
	{
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			Settings->SetTimeDilation(Combined);
		}
	}
	bTimeHeld = !TimeScales.IsEmpty();
}

// ---------------------------------------------------------------- the sun and the sky

void UDreamNarrativeStageArbiter::SetLightShare(UDreamNarrativeStageDirector* Director, float Share)
{
	UWorld* World = GetWorld();
	if (!Director || !World)
	{
		return;
	}
	if (FMath::IsNearlyEqual(Share, 1.f))
	{
		ReleaseLightShare(Director);
		return;
	}
	if (!bLightsHeld)
	{
		LightOriginals.Reset();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<ULightComponentBase*> Lights(*It);
			for (ULightComponentBase* Light : Lights)
			{
				if (Light && (Light->IsA<UDirectionalLightComponent>() || Light->IsA<USkyLightComponent>()))
				{
					LightOriginals.Emplace(Light, Light->Intensity);
				}
			}
		}
		bLightsHeld = true;
	}
	LightShares.Add(Director, Share);
	ApplyLights();
}

void UDreamNarrativeStageArbiter::ReleaseLightShare(UDreamNarrativeStageDirector* Director)
{
	if (LightShares.Remove(Director) > 0)
	{
		ApplyLights();
	}
}

void UDreamNarrativeStageArbiter::ApplyLights()
{
	if (!bLightsHeld)
	{
		return;
	}
	float Share = 1.f;
	for (const TPair<FDirectorRef, float>& Pair : LightShares)
	{
		Share *= Pair.Value;
	}
	for (const TPair<TWeakObjectPtr<ULightComponentBase>, float>& Light : LightOriginals)
	{
		if (ULightComponentBase* Component = Light.Key.Get())
		{
			SetLightIntensity(Component, Light.Value * Share);
		}
	}
	if (LightShares.IsEmpty())
	{
		LightOriginals.Reset();
		bLightsHeld = false;
	}
}

// ---------------------------------------------------------------- the HUD

void UDreamNarrativeStageArbiter::HideHud(UDreamNarrativeStageDirector* Director, AHUD* Hud)
{
	if (!Director || !Hud)
	{
		return;
	}
	FHudHold* Hold = Huds.FindByPredicate([Hud](const FHudHold& Each) { return Each.Hud.Get() == Hud; });
	const bool bFirst = Hold == nullptr;
	if (bFirst)
	{
		Hold = &Huds.AddDefaulted_GetRef();
		Hold->Hud = Hud;
		Hold->bWasShown = Hud->bShowHUD;
		Hud->bShowHUD = false;
	}
	Hold->Holders.AddUnique(Director);
	if (bFirst)
	{
		OnHudVisibility.Broadcast(false);
	}
}

void UDreamNarrativeStageArbiter::ReleaseHud(UDreamNarrativeStageDirector* Director)
{
	int32 Shown = 0;
	for (int32 Index = Huds.Num() - 1; Index >= 0; --Index)
	{
		FHudHold& Hold = Huds[Index];
		Hold.Holders.RemoveAll([Director](const FDirectorRef& Each) { return !Each.IsValid() || Each.Get() == Director; });
		if (Hold.Holders.IsEmpty())
		{
			if (AHUD* Hud = Hold.Hud.Get()) { Hud->bShowHUD = Hold.bWasShown; }
			Huds.RemoveAt(Index);
			++Shown;
		}
	}
	// Listeners may hide it again: told once the list is settled.
	for (int32 Each = 0; Each < Shown; ++Each)
	{
		OnHudVisibility.Broadcast(true);
	}
}

// ---------------------------------------------------------------- the view

void UDreamNarrativeStageArbiter::TakeView(UDreamNarrativeStageDirector* Director, APlayerController* Controller, AActor* Camera)
{
	if (!Director || !Controller)
	{
		return;
	}
	ReleaseView(Director, false);
	FViewStack* Stack = Views.FindByPredicate([Controller](const FViewStack& Each) { return Each.Controller.Get() == Controller; });
	if (!Stack)
	{
		Stack = &Views.AddDefaulted_GetRef();
		Stack->Controller = Controller;
		Stack->Original = Controller->GetViewTarget();
	}
	Stack->Holds.Add({ Director, Camera });
}

bool UDreamNarrativeStageArbiter::IsShowingView(const UDreamNarrativeStageDirector* Director) const
{
	for (const FViewStack& Stack : Views)
	{
		if (Stack.Holds.Num() > 0 && Stack.Holds.Last().Director.Get() == Director)
		{
			return true;
		}
	}
	return false;
}

AActor* UDreamNarrativeStageArbiter::GetViewBeneath(const UDreamNarrativeStageDirector* Director) const
{
	for (const FViewStack& Stack : Views)
	{
		const int32 Index = Stack.Holds.IndexOfByPredicate([Director](const FViewHold& Hold) { return Hold.Director.Get() == Director; });
		if (Index == INDEX_NONE)
		{
			continue;
		}
		for (int32 Below = Index - 1; Below >= 0; --Below)
		{
			AActor* Camera = Stack.Holds[Below].Camera.Get();
			if (IsValid(Camera) && Stack.Holds[Below].Director.IsValid())
			{
				return Camera;
			}
		}
		if (AActor* Original = Stack.Original.Get(); IsValid(Original))
		{
			return Original;
		}
		APlayerController* Controller = Stack.Controller.Get();
		return Controller && Controller->GetPawn() ? static_cast<AActor*>(Controller->GetPawn()) : Controller;
	}
	return nullptr;
}

void UDreamNarrativeStageArbiter::ReleaseView(UDreamNarrativeStageDirector* Director, bool bSetView)
{
	for (int32 Index = Views.Num() - 1; Index >= 0; --Index)
	{
		FViewStack& Stack = Views[Index];
		const int32 Hold = Stack.Holds.IndexOfByPredicate([Director](const FViewHold& Each) { return Each.Director.Get() == Director; });
		if (Hold == INDEX_NONE)
		{
			continue;
		}
		AActor* Back = bSetView && Hold == Stack.Holds.Num() - 1 ? GetViewBeneath(Director) : nullptr;
		APlayerController* Controller = Stack.Controller.Get();
		Stack.Holds.RemoveAt(Hold);
		if (Stack.Holds.IsEmpty())
		{
			Views.RemoveAt(Index);
		}
		if (Back && IsValid(Controller))
		{
			Controller->SetViewTargetWithBlend(Back, 0.f, VTBlend_Cubic, 0.f, false);
		}
	}
}

// ---------------------------------------------------------------- directors gone

void UDreamNarrativeStageArbiter::PurgeStale()
{
	auto IsStale = [](const FDirectorRef& Director) { return !Director.IsValid(); };
	bool bTime = false;
	for (auto It = TimeScales.CreateIterator(); It; ++It)
	{
		if (IsStale(It.Key())) { It.RemoveCurrent(); bTime = true; }
	}
	if (bTime) { ApplyTime(); }
	bool bLights = false;
	for (auto It = LightShares.CreateIterator(); It; ++It)
	{
		if (IsStale(It.Key())) { It.RemoveCurrent(); bLights = true; }
	}
	if (bLights) { ApplyLights(); }
	for (const FHudHold& Hold : Huds)
	{
		if (Hold.Holders.ContainsByPredicate(IsStale))
		{
			// ReleaseHud drops the stale holders of every HUD on the way.
			ReleaseHud(nullptr);
			break;
		}
	}
	for (int32 Index = Views.Num() - 1; Index >= 0; --Index)
	{
		FViewStack& Stack = Views[Index];
		const bool bTopGone = Stack.Holds.Num() > 0 && IsStale(Stack.Holds.Last().Director);
		Stack.Holds.RemoveAll([&IsStale](const FViewHold& Hold) { return IsStale(Hold.Director); });
		APlayerController* Controller = Stack.Controller.Get();
		if (bTopGone && IsValid(Controller))
		{
			AActor* Back = Stack.Holds.Num() > 0 ? Stack.Holds.Last().Camera.Get() : Stack.Original.Get();
			if (!IsValid(Back)) { Back = Controller->GetPawn() ? static_cast<AActor*>(Controller->GetPawn()) : Controller; }
			Controller->SetViewTargetWithBlend(Back, 0.f, VTBlend_Cubic, 0.f, false);
		}
		if (Stack.Holds.IsEmpty())
		{
			Views.RemoveAt(Index);
		}
	}
}
