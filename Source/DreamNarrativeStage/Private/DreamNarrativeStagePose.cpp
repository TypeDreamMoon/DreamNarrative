// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeStagePose.h"
#include "DreamNarrativeSettings.h"
#include "GameFramework/Actor.h"

namespace
{
	float Progress(float Time, float Start, float Over)
	{
		if (Time < Start) { return 0.f; }
		return Over <= 0.f ? 1.f : FMath::Clamp((Time - Start) / Over, 0.f, 1.f);
	}

	/** Smoothstep: eases in and out. */
	float Ease(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}

	/** Smootherstep: the harder ease of a whip. */
	float EaseHard(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * X * (X * (X * 6.f - 15.f) + 10.f);
	}

	/** Whether a range or held entry covers Time: a range up to (not including) its end, a point from its start on. */
	bool Holds(const FDnaStageEntry& Entry, float Time)
	{
		return Entry.bRange ? (Time >= Entry.Start && Time < Entry.End) : Time >= Entry.Start;
	}

	/**
	 * The entries in the order the stage meets them: the timed ones by start, then the restore block's
	 * at the stage's end. Only those that have started by Time.
	 */
	template <typename FVisitor>
	void ForEachStarted(const FDnaStageData& Stage, float Time, FVisitor&& Visit)
	{
		for (const FDnaStageEntry& Entry : Stage.Entries)
		{
			if (Entry.Start > Time) { break; }
			Visit(Entry, Entry.Start);
		}
		if (Time >= Stage.Duration)
		{
			for (const FDnaStageEntry& Entry : Stage.Restore)
			{
				Visit(Entry, Stage.Duration);
			}
		}
	}
}

// ---------------------------------------------------------------- data

float FDnaStageData::GetTail() const
{
	float Tail = 0.f;
	for (const FDnaStageEntry& Entry : Entries)
	{
		if (!DreamNarrative::IsDnaStageCue(Entry))
		{
			Tail = FMath::Max(Tail, FMath::Max(Entry.Start + Entry.Over, Entry.End) - Duration);
		}
	}
	for (const FDnaStageEntry& Entry : Restore)
	{
		Tail = FMath::Max(Tail, Entry.Over);
	}
	return FMath::Max(Tail, 0.f);
}

namespace DreamNarrative
{
	const TCHAR* LexToString(EDnaStageTrack Track)
	{
		switch (Track)
		{
		case EDnaStageTrack::Camera: return TEXT("camera");
		case EDnaStageTrack::Light: return TEXT("light");
		case EDnaStageTrack::Mask: return TEXT("mask");
		case EDnaStageTrack::Vfx: return TEXT("vfx");
		case EDnaStageTrack::Ui: return TEXT("ui");
		case EDnaStageTrack::Anim: return TEXT("anim");
		case EDnaStageTrack::Game: return TEXT("game");
		case EDnaStageTrack::Sequence: return TEXT("sequence");
		}
		return TEXT("?");
	}

	const TCHAR* LexToString(EDnaStageVerb Verb)
	{
		switch (Verb)
		{
		case EDnaStageVerb::None: return TEXT("none");
		case EDnaStageVerb::BlendFromGame: return TEXT("blend from game");
		case EDnaStageVerb::BlendBack: return TEXT("blend back");
		case EDnaStageVerb::Cut: return TEXT("cut");
		case EDnaStageVerb::BlendTo: return TEXT("blend");
		case EDnaStageVerb::Whip: return TEXT("whip");
		case EDnaStageVerb::Push: return TEXT("push");
		case EDnaStageVerb::RampDark: return TEXT("ramp dark");
		case EDnaStageVerb::RestoreLight: return TEXT("restore");
		case EDnaStageVerb::KeyLight: return TEXT("key");
		case EDnaStageVerb::Distort: return TEXT("distort");
		case EDnaStageVerb::Exposure: return TEXT("exposure");
		case EDnaStageVerb::Streaks: return TEXT("streaks");
		case EDnaStageVerb::Comic: return TEXT("comic");
		case EDnaStageVerb::CutBlack: return TEXT("cut black");
		case EDnaStageVerb::FadeBlack: return TEXT("fade black");
		case EDnaStageVerb::FadeWhite: return TEXT("fade white");
		case EDnaStageVerb::Spawn: return TEXT("spawn");
		case EDnaStageVerb::StopVfx: return TEXT("stop");
		case EDnaStageVerb::HideHud: return TEXT("hide hud");
		case EDnaStageVerb::ShowHud: return TEXT("show hud");
		case EDnaStageVerb::Text: return TEXT("text");
		case EDnaStageVerb::ClearText: return TEXT("clear");
		case EDnaStageVerb::Montage: return TEXT("montage");
		case EDnaStageVerb::InputLock: return TEXT("input lock");
		case EDnaStageVerb::Hitstop: return TEXT("hitstop");
		case EDnaStageVerb::TimeScale: return TEXT("time");
		case EDnaStageVerb::DamageWindow: return TEXT("damage window");
		case EDnaStageVerb::PlaySequence: return TEXT("play");
		}
		return TEXT("?");
	}

	namespace
	{
		FMinimalViewInfo ResolveShot(const FDnaStageData& Stage, FName Shot, float Push, const FMinimalViewInfo& GameView, TFunctionRef<AActor*(FName)> Resolve)
		{
			FMinimalViewInfo View = GameView;
			if (const FDnaStageShot* Data = Shot.IsNone() ? nullptr : Stage.FindShot(Shot))
			{
				const AActor* Anchor = Resolve(!Data->Anchor.IsNone() ? Data->Anchor : Data->LookAt);
				const AActor* Look = Data->LookAt.IsNone() ? Anchor : Resolve(Data->LookAt);
				const FTransform Frame = Anchor ? Anchor->GetActorTransform() : FTransform::Identity;
				View.Location = Frame.TransformPosition(Data->Offset);
				const FVector Target = Look ? Look->GetActorLocation() : Frame.GetLocation();
				if (Data->bHasRotation)
				{
					View.Rotation = Frame.TransformRotation(Data->Rotation.Quaternion()).Rotator();
				}
				else if (!(Target - View.Location).IsNearlyZero())
				{
					View.Rotation = (Target - View.Location).Rotation();
				}
				if (Data->Fov > 0.f)
				{
					View.FOV = Data->Fov;
				}
			}
			View.Location += View.Rotation.Vector() * Push;
			return View;
		}
	}

	FMinimalViewInfo EvaluateDnaStageView(const FDnaStageData& Stage, const FDnaStageCameraPose& Camera, const FMinimalViewInfo& GameView, TFunctionRef<AActor*(FName)> Resolve)
	{
		const FMinimalViewInfo From = ResolveShot(Stage, Camera.From, Camera.FromPush, GameView, Resolve);
		const FMinimalViewInfo To = ResolveShot(Stage, Camera.To, Camera.Push, GameView, Resolve);
		const float Alpha = FMath::Clamp(Camera.Alpha, 0.f, 1.f);
		FMinimalViewInfo View = To;
		View.Location = FMath::Lerp(From.Location, To.Location, Alpha);
		View.Rotation = FQuat::Slerp(From.Rotation.Quaternion(), To.Rotation.Quaternion(), Alpha).Rotator();
		View.FOV = FMath::Lerp(From.FOV, To.FOV, Alpha);
		return View;
	}

	bool IsDnaStageCue(const FDnaStageEntry& Entry)
	{
		switch (Entry.Verb)
		{
		case EDnaStageVerb::Montage:
		case EDnaStageVerb::Spawn:
		case EDnaStageVerb::StopVfx:
		case EDnaStageVerb::PlaySequence:
			return true;
		default:
			return false;
		}
	}

	float GetDnaMaskEnvelope(const FDnaStageEntry& Entry, float Time)
	{
		if (Time < Entry.Start)
		{
			return 0.f;
		}
		float Fade = 0.f;
		switch (Entry.Verb)
		{
		case EDnaStageVerb::FadeBlack:
		case EDnaStageVerb::FadeWhite:
			Fade = Entry.Over;
			break;
		case EDnaStageVerb::Distort:
		case EDnaStageVerb::Exposure:
		case EDnaStageVerb::Streaks:
			// A pulse: up to the middle of its range and down again.
			Fade = Entry.bRange ? (Entry.End - Entry.Start) * 0.5f : 0.f;
			break;
		default:
			break;
		}
		if (!Entry.bRange)
		{
			return Fade > 0.f ? FMath::Clamp((Time - Entry.Start) / Fade, 0.f, 1.f) : 1.f;
		}
		const float Length = Entry.End - Entry.Start;
		Fade = FMath::Min(Fade, Length * 0.5f);
		if (Fade <= 0.f)
		{
			return Time < Entry.End ? 1.f : 0.f;
		}
		if (Time > Entry.End)
		{
			return 0.f;
		}
		const float Up = (Time - Entry.Start) / Fade;
		const float Down = (Entry.End - Time) / Fade;
		return FMath::Clamp(FMath::Min(Up, Down), 0.f, 1.f);
	}

	FDnaStagePose SampleDnaStage(const FDnaStageData& Stage, float Time, float UnitSeconds)
	{
		FDnaStagePose Pose;
		UnitSeconds = UnitSeconds > 0.f ? UnitSeconds : 1.f;

		// -- camera: walk the moves; the last one decides where the camera is
		FDnaStageCameraPose& Camera = Pose.Camera;
		float MoveStart = 0.f, MoveOver = 0.f, ReturnEnd = 0.f;
		bool bWhip = false;
		TArray<const FDnaStageEntry*, TInlineAllocator<4>> Pushes;
		TArray<float, TInlineAllocator<4>> PushStarts;
		auto PushAt = [&](float At)
		{
			float Sum = 0.f;
			for (int32 Index = 0; Index < Pushes.Num(); ++Index)
			{
				Sum += Pushes[Index]->Amount * Ease(Progress(At, PushStarts[Index], Pushes[Index]->Over));
			}
			return Sum;
		};
		auto TakeOver = [&](float BlendIn)
		{
			if (!Camera.bActive || Camera.bReturning)
			{
				const int32 Cuts = Camera.Cuts;
				Camera = FDnaStageCameraPose();
				Camera.Cuts = Cuts;
				Camera.bActive = true;
				Camera.BlendIn = BlendIn * UnitSeconds;
				Pushes.Reset();
				PushStarts.Reset();
				MoveOver = 0.f;
			}
		};

		// -- light
		float RampFrom = 1.f, RampTo = 1.f, RampStart = 0.f, RampOver = 0.f;
		TMap<FName, bool> Keys;
		const float Floor = UDreamNarrativeSettings::Get()->LightRampFloor;

		bool bHudHidden = false;
		bool bTextShown = false;

		ForEachStarted(Stage, Time, [&](const FDnaStageEntry& Entry, float Start)
		{
			switch (Entry.Track)
			{
			case EDnaStageTrack::Camera:
				switch (Entry.Verb)
				{
				case EDnaStageVerb::BlendFromGame:
					if (Camera.bActive && !Camera.bReturning) { break; }
					TakeOver(Entry.Over);
					break;
				case EDnaStageVerb::BlendBack:
					if (!Camera.bActive || Camera.bReturning) { break; }
					Camera.bReturning = true;
					Camera.BlendOut = Entry.Over * UnitSeconds;
					ReturnEnd = Start + Entry.Over;
					break;
				case EDnaStageVerb::Cut:
					TakeOver(0.f);
					Camera.From = Camera.To = Entry.Target;
					Camera.FromPush = Camera.Push = 0.f;
					Camera.Alpha = 1.f;
					MoveOver = 0.f;
					Pushes.Reset();
					PushStarts.Reset();
					++Camera.Cuts;
					break;
				case EDnaStageVerb::BlendTo:
				case EDnaStageVerb::Whip:
					TakeOver(0.f);
					// From where the camera is now; a move still under way is taken as arrived.
					Camera.FromPush = PushAt(Start);
					Camera.From = Camera.To;
					Camera.To = Entry.Target;
					Pushes.Reset();
					PushStarts.Reset();
					MoveStart = Start;
					bWhip = Entry.Verb == EDnaStageVerb::Whip;
					MoveOver = bWhip ? DnaWhipSeconds / UnitSeconds : Entry.Over;
					break;
				case EDnaStageVerb::Push:
					TakeOver(0.f);
					Pushes.Add(&Entry);
					PushStarts.Add(Start);
					break;
				default:
					break;
				}
				break;

			case EDnaStageTrack::Light:
				if (Entry.Verb == EDnaStageVerb::RampDark || Entry.Verb == EDnaStageVerb::RestoreLight)
				{
					const float Now = FMath::Lerp(RampFrom, RampTo, Progress(Start, RampStart, RampOver));
					RampFrom = Now;
					RampTo = Entry.Verb == EDnaStageVerb::RestoreLight ? 1.f : (Entry.Amount >= 0.f ? Entry.Amount : Floor);
					RampStart = Start;
					RampOver = Entry.Over;
				}
				else if (Entry.Verb == EDnaStageVerb::KeyLight)
				{
					Keys.Add(Entry.Target, Entry.bRange ? (Entry.bFlag && Holds(Entry, Time)) : Entry.bFlag);
				}
				break;

			case EDnaStageTrack::Mask:
			{
				const float Envelope = GetDnaMaskEnvelope(Entry, Time);
				switch (Entry.Verb)
				{
				case EDnaStageVerb::Distort: Pose.Distort = FMath::Max(Pose.Distort, Entry.Amount * Envelope); break;
				case EDnaStageVerb::Exposure:
					if (FMath::Abs(Entry.Amount * Envelope) > FMath::Abs(Pose.ExposureEv)) { Pose.ExposureEv = Entry.Amount * Envelope; }
					break;
				case EDnaStageVerb::Streaks: Pose.Streaks = FMath::Max(Pose.Streaks, Envelope); break;
				case EDnaStageVerb::Comic: Pose.Comic = FMath::Max(Pose.Comic, Envelope); break;
				case EDnaStageVerb::CutBlack:
				case EDnaStageVerb::FadeBlack: Pose.Black = FMath::Max(Pose.Black, Envelope); break;
				case EDnaStageVerb::FadeWhite: Pose.White = FMath::Max(Pose.White, Envelope); break;
				default: break;
				}
				break;
			}

			case EDnaStageTrack::Ui:
				switch (Entry.Verb)
				{
				case EDnaStageVerb::HideHud:
					if (Entry.bRange) { bHudHidden |= Holds(Entry, Time); }
					else { bHudHidden = true; }
					break;
				case EDnaStageVerb::ShowHud:
					bHudHidden = false;
					break;
				case EDnaStageVerb::Text:
					if (!Entry.bRange || Holds(Entry, Time))
					{
						Pose.Text = Entry.Text;
						Pose.TextStyle = Entry.Style;
						bTextShown = true;
					}
					else if (bTextShown && Pose.Text.EqualTo(Entry.Text))
					{
						bTextShown = false;
					}
					break;
				case EDnaStageVerb::ClearText:
					bTextShown = false;
					break;
				default:
					break;
				}
				break;

			case EDnaStageTrack::Game:
				if (!Holds(Entry, Time)) { break; }
				switch (Entry.Verb)
				{
				case EDnaStageVerb::InputLock: Pose.bInputLocked = true; break;
				case EDnaStageVerb::Hitstop:
				case EDnaStageVerb::TimeScale: Pose.TimeDilation *= FMath::Max(Entry.Amount, 0.0001f); break;
				case EDnaStageVerb::DamageWindow: Pose.bDamageWindow = true; break;
				default: break;
				}
				break;

			default:
				break;
			}
		});

		if (Camera.bActive)
		{
			if (Camera.bReturning && Time >= ReturnEnd)
			{
				const int32 Cuts = Camera.Cuts;
				Camera = FDnaStageCameraPose();
				Camera.Cuts = Cuts;
			}
			else
			{
				const float Linear = MoveOver > 0.f ? Progress(Time, MoveStart, MoveOver) : 1.f;
				Camera.Alpha = bWhip ? EaseHard(Linear) : Ease(Linear);
				if (Camera.Alpha >= 1.f) { Camera.From = Camera.To; Camera.FromPush = 0.f; }
				Camera.Push = PushAt(Time);
			}
		}

		Pose.LightShare = FMath::Lerp(RampFrom, RampTo, Progress(Time, RampStart, RampOver));
		for (const TPair<FName, bool>& Key : Keys)
		{
			if (Key.Value) { Pose.KeyLights.Add(Key.Key); }
		}
		Pose.bHudHidden = bHudHidden;
		if (!bTextShown)
		{
			Pose.Text = FText::GetEmpty();
			Pose.TextStyle = NAME_None;
		}
		return Pose;
	}
}

// ---------------------------------------------------------------- pose

bool FDnaStageCameraPose::operator==(const FDnaStageCameraPose& Other) const
{
	return bActive == Other.bActive && bReturning == Other.bReturning && From == Other.From && To == Other.To && Cuts == Other.Cuts
		&& FMath::IsNearlyEqual(BlendIn, Other.BlendIn) && FMath::IsNearlyEqual(BlendOut, Other.BlendOut)
		&& FMath::IsNearlyEqual(Alpha, Other.Alpha, 1e-4f) && FMath::IsNearlyEqual(FromPush, Other.FromPush, 1e-3f) && FMath::IsNearlyEqual(Push, Other.Push, 1e-3f);
}

bool FDnaStagePose::NearlyEquals(const FDnaStagePose& Other, float Tolerance) const
{
	return Camera == Other.Camera
		&& FMath::IsNearlyEqual(LightShare, Other.LightShare, Tolerance) && KeyLights == Other.KeyLights
		&& FMath::IsNearlyEqual(Distort, Other.Distort, Tolerance) && FMath::IsNearlyEqual(ExposureEv, Other.ExposureEv, Tolerance)
		&& FMath::IsNearlyEqual(Streaks, Other.Streaks, Tolerance) && FMath::IsNearlyEqual(Comic, Other.Comic, Tolerance)
		&& FMath::IsNearlyEqual(Black, Other.Black, Tolerance) && FMath::IsNearlyEqual(White, Other.White, Tolerance)
		&& bHudHidden == Other.bHudHidden && Text.EqualTo(Other.Text) && TextStyle == Other.TextStyle
		&& bInputLocked == Other.bInputLocked && FMath::IsNearlyEqual(TimeDilation, Other.TimeDilation, Tolerance) && bDamageWindow == Other.bDamageWindow;
}

FString FDnaStagePose::Describe() const
{
	TArray<FString> Parts;
	if (Camera.bActive)
	{
		Parts.Add(FString::Printf(TEXT("camera %s -> %s %.3f push %.1f%s cuts %d"), *Camera.From.ToString(), *Camera.To.ToString(), Camera.Alpha, Camera.Push,
			Camera.bReturning ? TEXT(" returning") : TEXT(""), Camera.Cuts));
	}
	if (LightShare != 1.f) { Parts.Add(FString::Printf(TEXT("light %.3f"), LightShare)); }
	for (const FName& Key : KeyLights) { Parts.Add(FString::Printf(TEXT("key %s"), *Key.ToString())); }
	if (Distort > 0.f) { Parts.Add(FString::Printf(TEXT("distort %.3f"), Distort)); }
	if (ExposureEv != 0.f) { Parts.Add(FString::Printf(TEXT("exposure %.3f"), ExposureEv)); }
	if (Streaks > 0.f) { Parts.Add(FString::Printf(TEXT("streaks %.3f"), Streaks)); }
	if (Comic > 0.f) { Parts.Add(FString::Printf(TEXT("comic %.3f"), Comic)); }
	if (Black > 0.f) { Parts.Add(FString::Printf(TEXT("black %.3f"), Black)); }
	if (White > 0.f) { Parts.Add(FString::Printf(TEXT("white %.3f"), White)); }
	if (bHudHidden) { Parts.Add(TEXT("hud hidden")); }
	if (!Text.IsEmpty()) { Parts.Add(FString::Printf(TEXT("text %s"), *Text.ToString())); }
	if (bInputLocked) { Parts.Add(TEXT("input locked")); }
	if (TimeDilation != 1.f) { Parts.Add(FString::Printf(TEXT("time %.3f"), TimeDilation)); }
	if (bDamageWindow) { Parts.Add(TEXT("damage window")); }
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("at rest"));
}
