// Copyright DreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraActor.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "DnaSequenceImport.h"
#include "DreamNarrativeStagePose.h"
#include "Lang/DnaParser.h"
#include "Lang/DnaStageCompiler.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDnaSequenceImportTest, "DreamNarrative.Editor.SequenceImport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDnaSequenceImportTest::RunTest(const FString& Parameters)
{
	using namespace UE::DreamNarrative;
	using namespace UE::DreamNarrative::Lang;

	// A six-second sequence: camera A dollies from x 100 to x 400 over its three seconds, camera B stands still.
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), TEXT("LS_对峙"), RF_Transient);
	Sequence->Initialize();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate Rate = MovieScene->GetTickResolution();
	const FFrameNumber Three = Rate.AsFrameNumber(3.0);
	MovieScene->SetPlaybackRange(FFrameNumber(0), Rate.AsFrameNumber(6.0).Value);

	auto AddCamera = [MovieScene](const TCHAR* Name, TFunctionRef<void(TArrayView<FMovieSceneDoubleChannel*>)> Animate)
	{
		const FGuid Binding = MovieScene->AddPossessable(Name, ACameraActor::StaticClass());
		UMovieScene3DTransformTrack* Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(Binding);
		UMovieScene3DTransformSection* Section = Cast<UMovieScene3DTransformSection>(Track->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>::All());
		Track->AddSection(*Section);
		Animate(Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>());
		return Binding;
	};
	const FGuid A = AddCamera(TEXT("镜头A"), [Three](TArrayView<FMovieSceneDoubleChannel*> Channels)
	{
		Channels[0]->AddLinearKey(FFrameNumber(0), 100.0);
		Channels[0]->AddLinearKey(Three, 400.0);
		Channels[2]->SetDefault(150.0);
		Channels[5]->SetDefault(90.0);
	});
	const FGuid B = AddCamera(TEXT("镜头B"), [](TArrayView<FMovieSceneDoubleChannel*> Channels)
	{
		Channels[1]->SetDefault(500.0);
		Channels[2]->SetDefault(200.0);
		Channels[4]->SetDefault(-10.0);
	});
	UMovieSceneCameraCutTrack* Cuts = Cast<UMovieSceneCameraCutTrack>(MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	Cuts->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(A), FFrameNumber(0));
	Cuts->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(B), Three);

	Editor::FDnaSequenceImport Options;
	Options.StageName = TEXT("对峙");
	FString Text;
	FText Error;
	if (!TestTrue(TEXT("imports"), Editor::MakeDnaStageFromSequence(*Sequence, Options, Text, Error)))
	{
		AddInfo(Error.ToString());
		return false;
	}
	AddInfo(Text);

	// It reads, and checks, as a stage.
	FDnaDocument Document;
	FDnaDiagnosticSink Diagnostics;
	ParseDnaDocument(FDnaSourceText(TEXT("Imported.dna"), Text), FDnaParseOptions(), Document, Diagnostics);
	FDnaStageData Stage;
	TArray<FDnaLineNode*> Roots;
	Document.GetRootsOfKind(EDnaLineKind::Stage, Roots);
	if (!TestEqual(TEXT("one stage"), Roots.Num(), 1))
	{
		return false;
	}
	CompileDnaStage(*Roots[0], Stage, Diagnostics);
	TestFalse(TEXT("without errors"), Diagnostics.HasErrors());
	TestEqual(TEXT("as long as the sequence"), Stage.Duration, 6.f, 0.01f);
	TestEqual(TEXT("a shot per cut and one where the moving camera ends"), Stage.Shots.Num(), 3);
	const FDnaStageEntry* SecondCut = Stage.Entries.FindByPredicate([](const FDnaStageEntry& Entry) { return Entry.Verb == EDnaStageVerb::Cut && Entry.Start > 1.f; });
	TestTrue(TEXT("the second cut at 3 s, hard"), SecondCut && FMath::IsNearlyEqual(SecondCut->Start, 3.f, 0.01f) && SecondCut->bFlag);

	// Played back with nothing bound to the anchor, the shots are where the cameras were in the world.
	auto Nobody = [](FName) -> AActor* { return nullptr; };
	const FDnaStagePose Half = DreamNarrative::SampleDnaStage(Stage, 1.5f);
	const FMinimalViewInfo AtHalf = DreamNarrative::EvaluateDnaStageView(Stage, Half.Camera, FMinimalViewInfo(), Nobody);
	TestEqual(TEXT("half way along the dolly"), AtHalf.Location.X, 250.0, 1.0);
	TestEqual(TEXT("at its height"), AtHalf.Location.Z, 150.0, 0.1);
	TestEqual(TEXT("facing as it did"), AtHalf.Rotation.Yaw, 90.0, 0.1);
	const FMinimalViewInfo AtFour = DreamNarrative::EvaluateDnaStageView(Stage, DreamNarrative::SampleDnaStage(Stage, 4.f).Camera, FMinimalViewInfo(), Nobody);
	TestEqual(TEXT("the second camera"), AtFour.Location.Y, 500.0, 0.1);
	TestEqual(TEXT("looking down a little"), AtFour.Rotation.Pitch, -10.0, 0.1);
	return !HasAnyErrors();
}

#endif
