// Copyright DreamMoon. All Rights Reserved.

#include "DnaSequenceImport.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "DreamNarrativeEditorModule.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Lang/DnaLexer.h"
#include "Lang/DnaProject.h"
#include "Misc/PackageName.h"
#include "LevelSequence.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DnaSequenceImport"

namespace UE::DreamNarrative::Editor
{
	namespace
	{
		/** Where a bound camera is at a frame, and its field of view (0 when unknown). */
		struct FCameraAt
		{
			FTransform Transform = FTransform::Identity;
			float Fov = 0.f;
			bool bFound = false;
		};

		FCameraAt CameraAt(UMovieScene& MovieScene, const FGuid& Binding, FFrameNumber Frame, UWorld* World)
		{
			FCameraAt Out;
			// The actor in the world, for a possessed camera: where it stands, and its lens.
			if (const FMovieScenePossessable* Possessable = World ? MovieScene.FindPossessable(Binding) : nullptr)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					if (It->GetActorLabel() == Possessable->GetName())
					{
						Out.Transform = It->GetActorTransform();
						if (const UCameraComponent* Camera = It->FindComponentByClass<UCameraComponent>())
						{
							Out.Fov = Camera->FieldOfView;
						}
						Out.bFound = true;
						break;
					}
				}
			}
			// The sequence's own animation of it, where there is one.
			const UMovieScene3DTransformTrack* Track = MovieScene.FindTrack<UMovieScene3DTransformTrack>(Binding);
			const TArray<UMovieSceneSection*> Sections = Track ? Track->GetAllSections() : TArray<UMovieSceneSection*>();
			for (UMovieSceneSection* Section : Sections)
			{
				if (!Section || !Section->GetRange().Contains(Frame))
				{
					continue;
				}
				const TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
				if (Channels.Num() < 6)
				{
					continue;
				}
				double Values[6];
				const FVector Location = Out.Transform.GetLocation();
				const FRotator Rotation = Out.Transform.Rotator();
				const double Fallback[6] = { Location.X, Location.Y, Location.Z, Rotation.Roll, Rotation.Pitch, Rotation.Yaw };
				for (int32 Index = 0; Index < 6; ++Index)
				{
					Values[Index] = Fallback[Index];
					Channels[Index]->Evaluate(FFrameTime(Frame), Values[Index]);
				}
				Out.Transform = FTransform(FRotator(Values[4], Values[5], Values[3]), FVector(Values[0], Values[1], Values[2]));
				Out.bFound = true;
				break;
			}
			return Out;
		}

		FString Number(double Value)
		{
			FString Text = FString::Printf(TEXT("%.1f"), FMath::RoundToDouble(Value * 10.0) / 10.0);
			Text.RemoveFromEnd(TEXT(".0"));
			return Text == TEXT("-0") ? FString(TEXT("0")) : Text;
		}

		FString Seconds(double Value)
		{
			FString Text = FString::Printf(TEXT("%.2f"), FMath::RoundToDouble(Value * 100.0) / 100.0);
			if (Text.EndsWith(TEXT("0"))) { Text.LeftChopInline(1); }
			return Text + TEXT("s");
		}

		/** A name the language takes: identifier characters kept, the rest underscores. */
		FString AsName(const FString& Raw, const TCHAR* Fallback)
		{
			FString Out;
			for (const TCHAR Char : Raw)
			{
				Out.AppendChar(Lang::IsDnaIdentifierChar(Char) ? Char : TEXT('_'));
			}
			if (Out.IsEmpty() || !Lang::IsDnaIdentifierStart(Out[0]))
			{
				Out = Fallback + Out;
			}
			return Out;
		}

		FString ShotLine(const FString& Name, const FCameraAt& At, const FString& Anchor)
		{
			const FVector L = At.Transform.GetLocation();
			const FRotator R = At.Transform.Rotator();
			FString Line = FString::Printf(TEXT("    %s  local (%s, %s, %s) rot (%s, %s, %s) of %s"), *Name,
				*Number(L.X), *Number(L.Y), *Number(L.Z), *Number(R.Pitch), *Number(R.Yaw), *Number(R.Roll), *Anchor);
			if (At.Fov > 0.f)
			{
				Line += TEXT(" fov ") + Number(At.Fov);
			}
			return Line;
		}
	}

	bool MakeDnaStageFromSequence(ULevelSequence& Sequence, const FDnaSequenceImport& Options, FString& OutText, FText& OutError)
	{
		UMovieScene* MovieScene = Sequence.GetMovieScene();
		const UMovieSceneCameraCutTrack* Cuts = MovieScene ? Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack()) : nullptr;
		if (!Cuts || Cuts->GetAllSections().Num() == 0)
		{
			OutError = FText::Format(LOCTEXT("NoCuts", "{0} has no camera cuts to import"), FText::FromString(Sequence.GetName()));
			return false;
		}
		const FFrameRate Rate = MovieScene->GetTickResolution();
		const TRange<FFrameNumber> Playback = MovieScene->GetPlaybackRange();
		const FFrameNumber First = Playback.HasLowerBound() ? Playback.GetLowerBoundValue() : FFrameNumber(0);
		const FFrameNumber Last = Playback.HasUpperBound() ? Playback.GetUpperBoundValue() : First;
		auto ToSeconds = [&Rate, First](FFrameNumber Frame) { return Rate.AsSeconds(FFrameTime(Frame - First)); };

		struct FCut
		{
			FGuid Camera;
			FFrameNumber Start;
		};
		TArray<FCut> Ordered;
		for (const UMovieSceneSection* Section : Cuts->GetAllSections())
		{
			const UMovieSceneCameraCutSection* Cut = Cast<UMovieSceneCameraCutSection>(Section);
			if (Cut && Cut->HasStartFrame())
			{
				Ordered.Add({ Cut->GetCameraBindingID().GetGuid(), Cut->GetInclusiveStartFrame() });
			}
		}
		Ordered.Sort([](const FCut& A, const FCut& B) { return A.Start < B.Start; });

		const FString Name = Options.StageName.IsEmpty() ? AsName(Sequence.GetName(), TEXT("过场_")) : Options.StageName;
		const FString Anchor = AsName(Options.Anchor, TEXT("原点_"));
		TArray<FString> Shots;
		TArray<FString> Camera;
		TSet<FString> Taken;
		for (int32 Index = 0; Index < Ordered.Num(); ++Index)
		{
			const FCut& Cut = Ordered[Index];
			const FFrameNumber End = Index + 1 < Ordered.Num() ? Ordered[Index + 1].Start : Last;
			const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Cut.Camera);
			FString Shot = AsName(Possessable ? Possessable->GetName() : FString::Printf(TEXT("镜头%d"), Index + 1), TEXT("镜头_"));
			for (int32 Suffix = 2; Taken.Contains(Shot); ++Suffix)
			{
				Shot = FString::Printf(TEXT("%s_%d"), *AsName(Possessable ? Possessable->GetName() : TEXT("镜头"), TEXT("镜头_")), Suffix);
			}
			Taken.Add(Shot);
			const FCameraAt From = CameraAt(*MovieScene, Cut.Camera, Cut.Start, Options.World);
			// The last frame of the cut, not the first of the next.
			const FCameraAt To = CameraAt(*MovieScene, Cut.Camera, FMath::Max(End - 1, Cut.Start), Options.World);
			Shots.Add(ShotLine(Shot, From, Anchor));
			Camera.Add(FString::Printf(TEXT("    %s  cut -> %s hard"), *Seconds(ToSeconds(Cut.Start)), *Shot));
			const bool bMoves = !From.Transform.Equals(To.Transform, 0.5) || !FMath::IsNearlyEqual(From.Fov, To.Fov, 0.1f);
			if (bMoves)
			{
				const FString Tail = Shot + TEXT("_尾");
				Taken.Add(Tail);
				Shots.Add(ShotLine(Tail, To, Anchor));
				Camera.Add(FString::Printf(TEXT("    %s  blend -> %s %s"), *Seconds(ToSeconds(Cut.Start)), *Tail, *Seconds(ToSeconds(End) - ToSeconds(Cut.Start))));
			}
		}
		const double Length = FMath::Max(ToSeconds(Last), 0.01);
		Camera.Add(FString::Printf(TEXT("    %s  blend back 0.0s"), *Seconds(Length)));

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("// 从关卡序列 %s 导入:每个镜头切换一个机位,镜头在动的再加一个结尾机位;机位按 %s 摆放(不绑定就是世界坐标)。"), *Sequence.GetPathName(), *Anchor));
		Lines.Add(TEXT("stage ") + Name);
		Lines.Add(TEXT("  duration ") + Seconds(Length));
		Lines.Add(TEXT("  shots"));
		Lines.Append(Shots);
		Lines.Add(TEXT("  camera"));
		Lines.Append(Camera);
		OutText = FString::Join(Lines, TEXT("\n")) + TEXT("\n");
		return true;
	}

	FString ImportDnaStageFromSequence(ULevelSequence& Sequence, const FDnaSequenceImport& Options, FText& OutError)
	{
		FString Text;
		if (!MakeDnaStageFromSequence(Sequence, Options, Text, OutError))
		{
			return FString();
		}
		const FString File = FPaths::Combine(Lang::GetDnaSourceRoot(), TEXT("Stages"), Sequence.GetName() + TEXT(".dna"));
		FString Existing;
		if (FFileHelper::LoadFileToString(Existing, *File) && !Existing.IsEmpty())
		{
			Existing.RemoveFromEnd(TEXT("\n"));
			Text = Existing + TEXT("\n\n") + Text;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), /*Tree=*/true);
		if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FText::Format(LOCTEXT("CannotWrite", "Cannot write {0}"), FText::FromString(File));
			return FString();
		}
		return File;
	}

	namespace
	{
		/** `dn.importsequence /Game/路径/LS_过场 [演出名]` */
		FAutoConsoleCommand ImportSequenceCommand(
			TEXT("dn.importsequence"),
			TEXT("Imports a level sequence's camera cuts as a stage: dn.importsequence /Game/Path/LS_Name [StageName]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() == 0)
				{
					UE_LOG(LogDreamNarrativeEditor, Display, TEXT("Usage: dn.importsequence /Game/Path/LS_Name [StageName]"));
					return;
				}
				FString Path = Args[0];
				if (!Path.Contains(TEXT(".")))
				{
					Path += TEXT(".") + FPackageName::GetShortName(Path);
				}
				ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *Path);
				if (!Sequence)
				{
					UE_LOG(LogDreamNarrativeEditor, Warning, TEXT("No level sequence at %s"), *Path);
					return;
				}
				FDnaSequenceImport Options;
				Options.StageName = Args.Num() > 1 ? Args[1] : FString();
				Options.World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
				FText Error;
				const FString File = ImportDnaStageFromSequence(*Sequence, Options, Error);
				if (File.IsEmpty())
				{
					UE_LOG(LogDreamNarrativeEditor, Warning, TEXT("%s"), *Error.ToString());
					return;
				}
				UE_LOG(LogDreamNarrativeEditor, Display, TEXT("Imported %s into %s"), *Sequence->GetName(), *File);
			}));
	}
}

#undef LOCTEXT_NAMESPACE
