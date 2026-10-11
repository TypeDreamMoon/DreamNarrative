// Copyright DreamMoon. All Rights Reserved.

#include "Lang/DnaStageCompiler.h"
#include "DreamNarrativeStagePose.h"
#include "Lang/DnaLexer.h"

#define LOCTEXT_NAMESPACE "DreamNarrativeLang"

namespace UE::DreamNarrative::Lang
{
	namespace
	{
		bool TrackOf(const FString& Word, EDnaStageTrack& Out)
		{
			static const TPair<const TCHAR*, EDnaStageTrack> Tracks[] = {
				{ TEXT("camera"), EDnaStageTrack::Camera }, { TEXT("light"), EDnaStageTrack::Light }, { TEXT("mask"), EDnaStageTrack::Mask },
				{ TEXT("vfx"), EDnaStageTrack::Vfx }, { TEXT("ui"), EDnaStageTrack::Ui }, { TEXT("anim"), EDnaStageTrack::Anim },
				{ TEXT("game"), EDnaStageTrack::Game }, { TEXT("sequence"), EDnaStageTrack::Sequence }, { TEXT("audio"), EDnaStageTrack::Audio }, { TEXT("blocking"), EDnaStageTrack::Blocking },
			};
			for (const TPair<const TCHAR*, EDnaStageTrack>& Track : Tracks)
			{
				if (Word == Track.Key)
				{
					Out = Track.Value;
					return true;
				}
			}
			return false;
		}

		/** The arguments of a stage line, read one by one. */
		struct FArgs
		{
			const TArray<FDnaToken>& Tokens;
			int32 Index = 0;

			explicit FArgs(const TArray<FDnaToken>& InTokens, int32 InIndex = 0) : Tokens(InTokens), Index(InIndex) {}

			bool AtEnd() const { return Index >= Tokens.Num() || Tokens[Index].Is(EDnaTokenKind::EndOfInput); }
			const FDnaToken* Peek() const { return AtEnd() ? nullptr : &Tokens[Index]; }

			bool Word(const TCHAR* Word)
			{
				if (!AtEnd() && Tokens[Index].IsWord(Word)) { ++Index; return true; }
				return false;
			}

			bool Is(EDnaTokenKind Kind)
			{
				if (!AtEnd() && Tokens[Index].Is(Kind)) { ++Index; return true; }
				return false;
			}

			bool Name(FString& Out)
			{
				if (!AtEnd() && (Tokens[Index].Is(EDnaTokenKind::Identifier) || Tokens[Index].Is(EDnaTokenKind::Keyword)))
				{
					Out = Tokens[Index++].Text;
					return true;
				}
				return false;
			}

			/** A number, with a sign if it has one. */
			bool Number(double& Out)
			{
				int32 At = Index;
				double Sign = 1.0;
				if (At < Tokens.Num() && (Tokens[At].Is(EDnaTokenKind::Plus) || Tokens[At].Is(EDnaTokenKind::Minus)))
				{
					Sign = Tokens[At].Is(EDnaTokenKind::Minus) ? -1.0 : 1.0;
					++At;
				}
				if (At < Tokens.Num() && (Tokens[At].Is(EDnaTokenKind::Int) || Tokens[At].Is(EDnaTokenKind::Float)))
				{
					Out = Sign * Tokens[At].Real;
					Index = At + 1;
					return true;
				}
				return false;
			}

			bool String(FString& Out)
			{
				if (!AtEnd() && Tokens[Index].Is(EDnaTokenKind::String))
				{
					Out = Tokens[Index++].Text;
					return true;
				}
				return false;
			}

			/** A content path (`/Game/FX/NS_Howl`, its pieces written together) or a bare asset name. */
			bool Path(FString& Out)
			{
				if (AtEnd())
				{
					return false;
				}
				if (!Tokens[Index].Is(EDnaTokenKind::Slash))
				{
					return Name(Out);
				}
				Out.Reset();
				int32 End = Tokens[Index].Span.Offset;
				while (!AtEnd() && Tokens[Index].Span.Offset == End)
				{
					const FDnaToken& Token = Tokens[Index];
					if (Token.Is(EDnaTokenKind::Slash)) { Out += TEXT("/"); }
					else if (Token.Is(EDnaTokenKind::Dot)) { Out += TEXT("."); }
					else if (Token.Is(EDnaTokenKind::Identifier) || Token.Is(EDnaTokenKind::Keyword) || Token.Is(EDnaTokenKind::Int)) { Out += Token.Text; }
					else { break; }
					End = Token.Span.End();
					++Index;
				}
				return Out.Len() > 1;
			}
		};

		struct FReadEntry
		{
			FDnaStageEntry Entry;
			FDnaSpan Span;
		};

		class FStageReader
		{
		public:
			FStageReader(FDnaStageData& InOut, FDnaDiagnosticSink& InDiagnostics) : Out(InOut), Diagnostics(InDiagnostics) {}

			bool Read(const FDnaLineNode& Root)
			{
				const FDnaLinePayload& P = Root.Payload;
				Out = FDnaStageData();
				Out.Name = FName(*P.Name);
				Out.WasName = P.WasName.IsEmpty() ? NAME_None : FName(*P.WasName);
				Out.SourceLine = Root.LineNumber();

				TArray<FDnaLineNode*> Lines;
				Root.GetContentChildren(Lines);

				// -- what the stage is: its base, its length, its gameplay tag; read first, the times depend on them
				const FDnaLineNode* DurationLine = nullptr;
				for (const FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Property)
					{
						ReadProperty(*Line);
					}
					else if (Line->Kind == EDnaLineKind::Duration)
					{
						DurationLine = Line;
					}
				}
				if (bLine)
				{
					Out.bLineRelative = true;
					Out.Duration = 1.f;
					if (DurationLine)
					{
						Fail(TEXT("DNS7004"), DurationLine->Raw.Span, LOCTEXT("LineDuration", "A stage on `base: line` lasts as long as its line; it takes no duration"));
					}
				}
				else if (DurationLine)
				{
					Out.Duration = static_cast<float>(DurationLine->Payload.Seconds);
				}
				else
				{
					Fail(TEXT("DNS7009"), Root.Raw.Span, FText::Format(LOCTEXT("NoDuration",
						"Stage '{0}' counts in seconds and needs a length: add `duration 6.0s`, or `base: line` to follow the line it is attached to"), FText::FromString(P.Name)));
				}

				// -- shots before the entries that cut to them
				for (const FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Shots || Line->Kind == EDnaLineKind::Marks)
					{
						TArray<FDnaLineNode*> Shots;
						Line->GetContentChildren(Shots);
						for (const FDnaLineNode* Shot : Shots)
						{
							if (Shot->Kind == EDnaLineKind::Shot) { ReadShot(*Shot, Line->Kind == EDnaLineKind::Marks ? Out.Marks : Out.Shots); }
						}
					}
				}
				for (const FDnaLineNode* Line : Lines)
				{
					if (Line->Kind == EDnaLineKind::Track)
					{
						EDnaStageTrack Track = EDnaStageTrack::Camera;
						TrackOf(Line->Payload.Name, Track);
						TArray<FDnaLineNode*> Entries;
						Line->GetContentChildren(Entries);
						for (const FDnaLineNode* Entry : Entries)
						{
							if (Entry->Kind == EDnaLineKind::StageEntry) { ReadTimedEntry(*Entry, Track); }
						}
					}
					else if (Line->Kind == EDnaLineKind::Restore)
					{
						TArray<FDnaLineNode*> Entries;
						Line->GetContentChildren(Entries);
						for (const FDnaLineNode* Entry : Entries)
						{
							if (Entry->Kind == EDnaLineKind::RestoreEntry) { ReadRestoreEntry(*Entry); }
						}
					}
				}

				Timed.StableSort([](const FReadEntry& A, const FReadEntry& B) { return A.Entry.Start < B.Entry.Start; });
				CheckCuts();
				CheckPutBack();
				for (const FReadEntry& Each : Timed) { Out.Entries.Add(Each.Entry); }
				for (const FReadEntry& Each : Restores) { Out.Restore.Add(Each.Entry); }
				return bOk;
			}

		private:
			FDnaStageData& Out;
			FDnaDiagnosticSink& Diagnostics;
			bool bOk = true;
			bool bLine = false;
			TArray<FReadEntry> Timed;
			TArray<FReadEntry> Restores;

			bool Fail(const TCHAR* Code, const FDnaSpan& Span, const FText& Message)
			{
				bOk = false;
				return Diagnostics.Error(Code, Span, Message);
			}

			const TCHAR* UnitWord() const { return bLine ? TEXT("percentages of the line, such as 40%") : TEXT("seconds, such as 0.3s"); }

			void ReadProperty(const FDnaLineNode& Line)
			{
				const FString& Key = Line.Payload.Name;
				const FString Value = Line.Payload.Value.TrimStartAndEnd();
				if (Key == TEXT("base"))
				{
					if (Value == TEXT("line")) { bLine = true; }
					else if (Value != TEXT("absolute"))
					{
						Fail(TEXT("DNS7006"), Line.Raw.Span, FText::Format(LOCTEXT("StageBase", "'{0}' is not a base; a stage counts in seconds (absolute) or follows its line (line)"), FText::FromString(Value)));
					}
					return;
				}
				if (Key == TEXT("gameplay"))
				{
					if (Value.IsEmpty() || Value.Contains(TEXT(" ")))
					{
						Fail(TEXT("DNS7006"), Line.Raw.Span, LOCTEXT("StageTag", "`gameplay:` takes one gameplay tag, such as Ability.Finisher"));
					}
					Out.GameplayTag = FName(*Value);
					return;
				}
				Fail(TEXT("DNS7006"), Line.Raw.Span, FText::Format(LOCTEXT("StageProperty", "'{0}' is not a stage property; a stage has base and gameplay"), FText::FromString(Key)));
			}

			void ReadShot(const FDnaLineNode& Line, TArray<FDnaStageShot>& Into)
			{
				FDnaStageShot Shot;
				Shot.Name = FName(*Line.Payload.Name);
				Shot.SourceLine = Line.LineNumber();
				if (Into.ContainsByPredicate([&Shot](const FDnaStageShot& Each) { return Each.Name == Shot.Name; }))
				{
					Fail(TEXT("DNS7006"), Line.Raw.Span, FText::Format(LOCTEXT("ShotTwice", "The stage already has a shot called '{0}'"), FText::FromString(Line.Payload.Name)));
					return;
				}
				FArgs Args(Line.Payload.Args);
				bool bLocal = false;
				while (!Args.AtEnd())
				{
					FString Name;
					double X = 0.0, Y = 0.0, Z = 0.0, Fov = 0.0;
					if (Args.Word(TEXT("rot")))
					{
						if (!(Args.Is(EDnaTokenKind::LeftParen) && Args.Number(X) && Args.Is(EDnaTokenKind::Comma) && Args.Number(Y)
							&& Args.Is(EDnaTokenKind::Comma) && Args.Number(Z) && Args.Is(EDnaTokenKind::RightParen)))
						{
							Fail(TEXT("DNS7006"), Line.Raw.Span, LOCTEXT("ShotRot", "`rot` takes pitch, yaw and roll in degrees, such as (0, -90, 0)"));
							return;
						}
						Shot.Rotation = FRotator(X, Y, Z);
						Shot.bHasRotation = true;
					}
					else if (Args.Word(TEXT("local")))
					{
						if (!(Args.Is(EDnaTokenKind::LeftParen) && Args.Number(X) && Args.Is(EDnaTokenKind::Comma) && Args.Number(Y)
							&& Args.Is(EDnaTokenKind::Comma) && Args.Number(Z) && Args.Is(EDnaTokenKind::RightParen)))
						{
							Fail(TEXT("DNS7006"), Line.Raw.Span, LOCTEXT("ShotLocal", "`local` takes a position in centimetres, such as (0, -120, 40)"));
							return;
						}
						Shot.Offset = FVector(X, Y, Z);
						bLocal = true;
					}
					else if (Args.Word(TEXT("of")) && Args.Name(Name)) { Shot.Anchor = FName(*Name); }
					else if (Args.Word(TEXT("look")) && Args.Name(Name)) { Shot.LookAt = FName(*Name); }
					else if (Args.Word(TEXT("fov")) && Args.Number(Fov) && Fov > 0.0 && Fov < 180.0) { Shot.Fov = static_cast<float>(Fov); }
					else
					{
						Fail(TEXT("DNS7006"), Line.Raw.Span, LOCTEXT("ShotArgs", "A shot reads `名 local (x, y, z) [rot (pitch, yaw, roll)] [of 角色] [look 角色] [fov 度数]`"));
						return;
					}
				}
				if (!bLocal)
				{
					Fail(TEXT("DNS7006"), Line.Raw.Span, LOCTEXT("ShotNoLocal", "A shot needs `local (x, y, z)`: where the camera is, in the space of the actor it is placed by"));
					return;
				}
				Into.Add(Shot);
			}

			/** A span (a blend, a fade, a ramp) in the stage's unit. Missing is 0 unless bRequired. */
			bool ReadSpan(FArgs& Args, const FDnaLineNode& Line, float& OutValue, bool bRequired)
			{
				const FDnaToken* Token = Args.Peek();
				if (Token && (Token->Is(EDnaTokenKind::Duration) || Token->Is(EDnaTokenKind::Percent) || Token->Is(EDnaTokenKind::Int) || Token->Is(EDnaTokenKind::Float)))
				{
					++Args.Index;
					const bool bPercent = Token->Is(EDnaTokenKind::Percent);
					if (bPercent != bLine)
					{
						return Fail(TEXT("DNS7004"), Token->Span, FText::Format(LOCTEXT("SpanUnit", "'{0}': this stage counts in {1}"), FText::FromString(Token->Text), FText::FromString(UnitWord())));
					}
					OutValue = static_cast<float>(bPercent ? Token->Real / 100.0 : Token->Real);
					return true;
				}
				OutValue = 0.f;
				if (bRequired)
				{
					return Fail(TEXT("DNS7006"), Line.Raw.Span, FText::Format(LOCTEXT("SpanMissing", "Expected how long it takes, in {0}"), FText::FromString(UnitWord())));
				}
				return true;
			}

			bool BadArgs(const FDnaLineNode& Line, const TCHAR* Usage)
			{
				return Fail(TEXT("DNS7006"), Line.Raw.Span, FText::Format(LOCTEXT("EntryUsage", "Expected `{0}`"), FText::FromString(Usage)));
			}

			/** The verb and its arguments; Args stands after the verb. */
			bool ReadVerb(const FDnaLineNode& Line, EDnaStageTrack Track, const FString& Verb, FArgs& Args, FDnaStageEntry& Entry)
			{
				FString Name;
				double Number = 0.0;
				switch (Track)
				{
				case EDnaStageTrack::Camera:
					if (Verb == TEXT("shake"))
					{
						if (!Args.Path(Entry.Asset)) { return BadArgs(Line, TEXT("shake /Game/路径/BP_震动 [scale 1.5]")); }
						Entry.Verb = EDnaStageVerb::Shake;
						Entry.Amount = 1.f;
						if (Args.Word(TEXT("scale")))
						{
							if (!Args.Number(Number) || Number <= 0.0) { return BadArgs(Line, TEXT("shake /Game/路径/BP_震动 [scale 1.5]")); }
							Entry.Amount = static_cast<float>(Number);
						}
						return true;
					}
					if (Verb == TEXT("blend"))
					{
						if (Args.Word(TEXT("from")))
						{
							if (!Args.Word(TEXT("game"))) { return BadArgs(Line, TEXT("blend from game 时长")); }
							Entry.Verb = EDnaStageVerb::BlendFromGame;
							return ReadSpan(Args, Line, Entry.Over, false);
						}
						if (Args.Word(TEXT("back")))
						{
							Entry.Verb = EDnaStageVerb::BlendBack;
							return ReadSpan(Args, Line, Entry.Over, false);
						}
						if (Args.Is(EDnaTokenKind::Arrow) && Args.Name(Name))
						{
							Entry.Verb = EDnaStageVerb::BlendTo;
							Entry.Target = FName(*Name);
							return ReadSpan(Args, Line, Entry.Over, true);
						}
						return BadArgs(Line, TEXT("blend from game 时长 | blend back 时长 | blend -> 机位 时长"));
					}
					if (Verb == TEXT("cut") || Verb == TEXT("whip"))
					{
						if (!(Args.Is(EDnaTokenKind::Arrow) && Args.Name(Name))) { return BadArgs(Line, Verb == TEXT("cut") ? TEXT("cut -> 机位 [hard]") : TEXT("whip -> 机位")); }
						Entry.Verb = Verb == TEXT("cut") ? EDnaStageVerb::Cut : EDnaStageVerb::Whip;
						Entry.Target = FName(*Name);
						// `hard`: a cut meant to be seen, as between the shots of a cutscene; no mask needs to hide it.
						Entry.bFlag = Verb == TEXT("cut") && Args.Word(TEXT("hard"));
						return true;
					}
					if (Verb == TEXT("push"))
					{
						if (!Args.Number(Number)) { return BadArgs(Line, TEXT("push 8cm over 6.0s")); }
						Entry.Verb = EDnaStageVerb::Push;
						Args.Word(TEXT("cm"));
						Entry.Amount = static_cast<float>(Number);
						if (Args.Word(TEXT("over")))
						{
							return ReadSpan(Args, Line, Entry.Over, true);
						}
						return true;
					}
					break;

				case EDnaStageTrack::Light:
					if (Verb == TEXT("ramp"))
					{
						if (!Args.Word(TEXT("dark"))) { return BadArgs(Line, TEXT("ramp dark 时长 [to 比例]")); }
						Entry.Verb = EDnaStageVerb::RampDark;
						Entry.Amount = -1.f;
						if (!ReadSpan(Args, Line, Entry.Over, true)) { return false; }
						if (Args.Word(TEXT("to")))
						{
							const FDnaToken* Token = Args.Peek();
							if (Token && Token->Is(EDnaTokenKind::Percent)) { ++Args.Index; Number = Token->Real / 100.0; }
							else if (!Args.Number(Number)) { return BadArgs(Line, TEXT("ramp dark 时长 to 0.2")); }
							if (Number < 0.0 || Number > 1.0) { return BadArgs(Line, TEXT("ramp dark 时长 to 0..1")); }
							Entry.Amount = static_cast<float>(Number);
						}
						return true;
					}
					if (Verb == TEXT("restore"))
					{
						Entry.Verb = EDnaStageVerb::RestoreLight;
						return ReadSpan(Args, Line, Entry.Over, false);
					}
					if (Verb == TEXT("key"))
					{
						if (!Args.Name(Name)) { return BadArgs(Line, TEXT("key 角色 on|off")); }
						Entry.Verb = EDnaStageVerb::KeyLight;
						Entry.Target = FName(*Name);
						if (Args.Word(TEXT("on"))) { Entry.bFlag = true; }
						else if (Args.Word(TEXT("off"))) { Entry.bFlag = false; }
						else { return BadArgs(Line, TEXT("key 角色 on|off")); }
						return true;
					}
					break;

				case EDnaStageTrack::Mask:
					if (Verb == TEXT("distort"))
					{
						Args.Word(TEXT("strength"));
						Entry.Verb = EDnaStageVerb::Distort;
						Entry.Amount = Args.Number(Number) ? static_cast<float>(Number) : 1.f;
						return true;
					}
					if (Verb == TEXT("exposure"))
					{
						Args.Word(TEXT("peak"));
						if (!Args.Number(Number)) { return BadArgs(Line, TEXT("exposure peak +1.5 ev")); }
						Args.Word(TEXT("ev"));
						Entry.Verb = EDnaStageVerb::Exposure;
						Entry.Amount = static_cast<float>(Number);
						return true;
					}
					if (Verb == TEXT("streaks")) { Entry.Verb = EDnaStageVerb::Streaks; return true; }
					if (Verb == TEXT("comic")) { Entry.Verb = EDnaStageVerb::Comic; return true; }
					if (Verb == TEXT("cut"))
					{
						if (!Args.Word(TEXT("black"))) { return BadArgs(Line, TEXT("cut black")); }
						Entry.Verb = EDnaStageVerb::CutBlack;
						return true;
					}
					if (Verb == TEXT("fade"))
					{
						if (Args.Word(TEXT("black"))) { Entry.Verb = EDnaStageVerb::FadeBlack; }
						else if (Args.Word(TEXT("white"))) { Entry.Verb = EDnaStageVerb::FadeWhite; }
						else { return BadArgs(Line, TEXT("fade black|white 时长")); }
						return ReadSpan(Args, Line, Entry.Over, false);
					}
					break;

				case EDnaStageTrack::Vfx:
					if (Verb == TEXT("spawn"))
					{
						if (!Args.Path(Entry.Asset)) { return BadArgs(Line, TEXT("spawn /Game/路径 [at 角色] [persist]")); }
						Entry.Verb = EDnaStageVerb::Spawn;
						while (!Args.AtEnd())
						{
							if (Args.Word(TEXT("at")) && Args.Name(Name)) { Entry.Target = FName(*Name); }
							else if (Args.Word(TEXT("persist"))) { Entry.bFlag = true; }
							else { return BadArgs(Line, TEXT("spawn /Game/路径 [at 角色] [persist]")); }
						}
						return true;
					}
					if (Verb == TEXT("stop")) { Entry.Verb = EDnaStageVerb::StopVfx; return true; }
					break;

				case EDnaStageTrack::Ui:
					if (Verb == TEXT("hide") || Verb == TEXT("show"))
					{
						if (!Args.Word(TEXT("hud"))) { return BadArgs(Line, Verb == TEXT("hide") ? TEXT("hide hud") : TEXT("show hud")); }
						Entry.Verb = Verb == TEXT("hide") ? EDnaStageVerb::HideHud : EDnaStageVerb::ShowHud;
						return true;
					}
					if (Verb == TEXT("text"))
					{
						FString Text;
						if (!Args.String(Text)) { return BadArgs(Line, TEXT("text 「…」 [style 名]")); }
						Entry.Verb = EDnaStageVerb::Text;
						Entry.Text = FText::FromString(Text);
						if (Args.Word(TEXT("style")))
						{
							if (!Args.Name(Name)) { return BadArgs(Line, TEXT("text 「…」 style 名")); }
							Entry.Style = FName(*Name);
						}
						return true;
					}
					if (Verb == TEXT("clear")) { Entry.Verb = EDnaStageVerb::ClearText; return true; }
					break;

				case EDnaStageTrack::Anim:
					if (Verb == TEXT("montage"))
					{
						if (!Args.Path(Entry.Asset)) { return BadArgs(Line, TEXT("montage 名 [actor 角色] [warp 目标 角色]")); }
						Entry.Verb = EDnaStageVerb::Montage;
						while (!Args.AtEnd())
						{
							FString Actor;
							if (Args.Word(TEXT("actor")) && Args.Name(Name)) { Entry.Target = FName(*Name); }
							else if (Args.Word(TEXT("warp")) && Args.Name(Name))
							{
								Args.Word(TEXT("to"));
								if (!Args.Name(Actor)) { return BadArgs(Line, TEXT("montage 名 warp 目标 角色")); }
								Entry.WarpTarget = FName(*Name);
								Entry.WarpActor = FName(*Actor);
							}
							else { return BadArgs(Line, TEXT("montage 名 [actor 角色] [warp 目标 角色]")); }
						}
						return true;
					}
					break;

				case EDnaStageTrack::Game:
					if (Verb == TEXT("input"))
					{
						if (!Args.Word(TEXT("lock"))) { return BadArgs(Line, TEXT("input lock")); }
						Entry.Verb = EDnaStageVerb::InputLock;
						return true;
					}
					if (Verb == TEXT("hitstop") || Verb == TEXT("time"))
					{
						if (!Args.Number(Number) || Number <= 0.0) { return BadArgs(Line, Verb == TEXT("hitstop") ? TEXT("hitstop 0.2x") : TEXT("time 0.5x")); }
						Args.Word(TEXT("x"));
						Entry.Verb = Verb == TEXT("hitstop") ? EDnaStageVerb::Hitstop : EDnaStageVerb::TimeScale;
						Entry.Amount = static_cast<float>(Number);
						return true;
					}
					if (Verb == TEXT("damage"))
					{
						if (!Args.Word(TEXT("window"))) { return BadArgs(Line, TEXT("damage window")); }
						Entry.Verb = EDnaStageVerb::DamageWindow;
						return true;
					}
					break;

				case EDnaStageTrack::Audio:
					if (Verb == TEXT("sound"))
					{
						if (!Args.Path(Entry.Asset)) { return BadArgs(Line, TEXT("sound 资源 [at 角色] [volume 0.8]")); }
						Entry.Verb = EDnaStageVerb::Sound;
						Entry.Amount = 1.f;
						while (!Args.AtEnd())
						{
							if (Args.Word(TEXT("at")) && Args.Name(Name)) { Entry.Target = FName(*Name); }
							else if (Args.Word(TEXT("volume")) && Args.Number(Number) && Number >= 0.0) { Entry.Amount = static_cast<float>(Number); }
							else { return BadArgs(Line, TEXT("sound 资源 [at 角色] [volume 0.8]")); }
						}
						return true;
					}
					if (Verb == TEXT("music"))
					{
						if (!Args.Path(Entry.Asset)) { return BadArgs(Line, TEXT("music 资源 [fade 2s] [keep]")); }
						Entry.Verb = EDnaStageVerb::Music;
						while (!Args.AtEnd())
						{
							if (Args.Word(TEXT("fade"))) { if (!ReadSpan(Args, Line, Entry.Over, true)) { return false; } }
							else if (Args.Word(TEXT("keep"))) { Entry.bFlag = true; }
							else { return BadArgs(Line, TEXT("music 资源 [fade 2s] [keep]")); }
						}
						return true;
					}
					break;

				case EDnaStageTrack::Blocking:
					if (Verb == TEXT("move") || Verb == TEXT("face"))
					{
						const bool bMove = Verb == TEXT("move");
						const TCHAR* Form = bMove ? TEXT("move 角色 -> 标记 [walk|run] [navigate]") : TEXT("face 角色 -> 目标 [over 时长]");
						FString Goal;
						if (!Args.Name(Name) || !Args.Is(EDnaTokenKind::Arrow) || !Args.Name(Goal)) { return BadArgs(Line, Form); }
						Entry.Verb = bMove ? EDnaStageVerb::Move : EDnaStageVerb::Face;
						Entry.Target = FName(*Name);
						Entry.Goal = FName(*Goal);
						while (!Args.AtEnd())
						{
							if (bMove && Args.Word(TEXT("walk"))) { Entry.bFlag = false; }
							else if (bMove && Args.Word(TEXT("run"))) { Entry.bFlag = true; }
							else if (bMove && Args.Word(TEXT("navigate"))) { Entry.bNavigate = true; }
							else if (!bMove && Args.Word(TEXT("over"))) { if (!ReadSpan(Args, Line, Entry.Over, true)) { return false; } }
							else { return BadArgs(Line, Form); }
						}
						return true;
					}
					if (Verb == TEXT("look"))
					{
						FString Goal;
						if (!Args.Name(Name) || !Args.Word(TEXT("at")) || !Args.Name(Goal)) { return BadArgs(Line, TEXT("look 角色 at 目标")); }
						Entry.Verb = EDnaStageVerb::Look;
						Entry.Target = FName(*Name);
						Entry.Goal = FName(*Goal);
						return true;
					}
					if (Verb == TEXT("back")) { Entry.Verb = EDnaStageVerb::BlockingBack; return true; }
					break;

				case EDnaStageTrack::Sequence:
					if (Verb == TEXT("play"))
					{
						if (!Args.Path(Entry.Asset)) { return BadArgs(Line, TEXT("play /Game/关卡序列")); }
						Entry.Verb = EDnaStageVerb::PlaySequence;
						return true;
					}
					break;
				}
				return Fail(TEXT("DNS7006"), Line.Raw.Span, FText::Format(LOCTEXT("UnknownVerb", "The {0} track has no '{1}'"),
					FText::FromString(::DreamNarrative::LexToString(Track)), FText::FromString(Verb)));
			}

			bool ExpectEnd(const FDnaLineNode& Line, const FArgs& Args)
			{
				if (const FDnaToken* Extra = Args.Peek())
				{
					return Fail(TEXT("DNS7006"), Extra->Span, FText::Format(LOCTEXT("EntryExtra", "'{0}' is more than this entry takes"), FText::FromString(Extra->Text)));
				}
				return true;
			}

			void ReadTimedEntry(const FDnaLineNode& Line, EDnaStageTrack Track)
			{
				const FDnaLinePayload& P = Line.Payload;
				FReadEntry Read;
				FDnaStageEntry& Entry = Read.Entry;
				Read.Span = Line.Raw.Span;
				Entry.Track = Track;
				Entry.SourceLine = Line.LineNumber();
				Entry.bRange = P.bIsRange;

				// -- the time, in the stage's unit
				if (bLine ? (!P.bPercent || (P.bIsRange && !P.bEndPercent)) : (P.bPercent || P.bEndPercent))
				{
					Fail(TEXT("DNS7004"), Line.Raw.Span, FText::Format(LOCTEXT("EntryUnit", "This stage counts in {0}"), FText::FromString(UnitWord())));
					return;
				}
				const double Scale = bLine ? 0.01 : 1.0;
				Entry.Start = static_cast<float>(P.TimeStart * Scale);
				Entry.End = static_cast<float>((P.bIsRange ? P.TimeEnd : P.TimeStart) * Scale);
				if (Entry.End < Entry.Start)
				{
					Fail(TEXT("DNS7005"), Line.Raw.Span, LOCTEXT("RangeBackwards", "This range ends before it starts"));
					return;
				}
				const float Length = Out.Duration;
				if (Length > 0.f && (Entry.Start > Length + KINDA_SMALL_NUMBER || Entry.End > Length + KINDA_SMALL_NUMBER))
				{
					Fail(TEXT("DNS7002"), Line.Raw.Span, bLine
						? LOCTEXT("PastLine", "This entry runs past the end of the line (100%)")
						: FText::Format(LOCTEXT("PastEnd", "This entry runs past the end of the stage ({0} s)"), FText::AsNumber(Length)));
					return;
				}

				FArgs Args(P.Args);
				if (!ReadVerb(Line, Track, P.Name, Args, Entry) || !ExpectEnd(Line, Args))
				{
					return;
				}
				if (Entry.Verb == EDnaStageVerb::BlockingBack)
				{
					Fail(TEXT("DNS7006"), Line.Raw.Span, LOCTEXT("BackOnlyRestore", "`blocking back` belongs under `restore`: it is what the stage does as it ends"));
					return;
				}

				// -- a range where one is needed, a point where one is
				bool bPointOnly = false, bRangeOnly = false;
				switch (Entry.Verb)
				{
				case EDnaStageVerb::BlendFromGame: case EDnaStageVerb::BlendBack: case EDnaStageVerb::Cut: case EDnaStageVerb::BlendTo:
				case EDnaStageVerb::Whip: case EDnaStageVerb::Push: case EDnaStageVerb::RampDark: case EDnaStageVerb::RestoreLight:
				case EDnaStageVerb::StopVfx: case EDnaStageVerb::ShowHud: case EDnaStageVerb::ClearText: case EDnaStageVerb::Montage:
				case EDnaStageVerb::PlaySequence: case EDnaStageVerb::Shake:
					bPointOnly = true;
					break;
				case EDnaStageVerb::Distort: case EDnaStageVerb::Exposure: case EDnaStageVerb::Streaks:
					bRangeOnly = true;
					break;
				default:
					break;
				}
				if ((bPointOnly && Entry.bRange) || (bRangeOnly && !Entry.bRange))
				{
					Fail(TEXT("DNS7007"), Line.Raw.Span, FText::Format(bPointOnly
						? LOCTEXT("PointOnly", "'{0}' happens at a moment; write one time, not a range")
						: LOCTEXT("RangeOnly", "'{0}' rises and falls over a range; write it as from..to, such as 1.9s..2.1s"),
						FText::FromString(::DreamNarrative::LexToString(Entry.Verb))));
					return;
				}
				if (Entry.Verb == EDnaStageVerb::Music && Entry.bFlag && Entry.bRange)
				{
					Fail(TEXT("DNS7007"), Line.Raw.Span, LOCTEXT("KeptMusicRange", "Music in a range lasts that range; `keep` is for music that plays on past the stage, so write one time"));
					return;
				}
				if ((Entry.Verb == EDnaStageVerb::Cut || Entry.Verb == EDnaStageVerb::BlendTo || Entry.Verb == EDnaStageVerb::Whip) && !Out.FindShot(Entry.Target))
				{
					Fail(TEXT("DNS7008"), Line.Raw.Span, FText::Format(LOCTEXT("UnknownShot", "The stage has no shot called '{0}'; declare it under `shots`"), FText::FromName(Entry.Target)));
					return;
				}
				Timed.Add(MoveTemp(Read));
			}

			void ReadRestoreEntry(const FDnaLineNode& Line)
			{
				FReadEntry Read;
				FDnaStageEntry& Entry = Read.Entry;
				Read.Span = Line.Raw.Span;
				Entry.SourceLine = Line.LineNumber();
				if (!TrackOf(Line.Payload.Name, Entry.Track))
				{
					Fail(TEXT("DNS7006"), Line.Raw.Span, FText::Format(LOCTEXT("RestoreTrack", "'{0}' is not a track; a restore entry starts with the track it puts back"), FText::FromString(Line.Payload.Name)));
					return;
				}
				FArgs Args(Line.Payload.Args);
				FString Verb;
				if (!Args.Name(Verb))
				{
					BadArgs(Line, TEXT("轨道 动词 参数…"));
					return;
				}
				if (!ReadVerb(Line, Entry.Track, Verb, Args, Entry) || !ExpectEnd(Line, Args))
				{
					return;
				}
				const bool bPutsBack = Entry.Verb == EDnaStageVerb::BlendBack || Entry.Verb == EDnaStageVerb::RestoreLight
					|| (Entry.Verb == EDnaStageVerb::KeyLight && !Entry.bFlag) || Entry.Verb == EDnaStageVerb::ShowHud
					|| Entry.Verb == EDnaStageVerb::ClearText || Entry.Verb == EDnaStageVerb::StopVfx || Entry.Verb == EDnaStageVerb::BlockingBack;
				if (!bPutsBack)
				{
					Fail(TEXT("DNS7006"), Line.Raw.Span, LOCTEXT("RestoreOnly",
						"`restore` holds what puts the world back: camera blend back, light restore, light key 角色 off, ui show hud, ui clear, vfx stop, blocking back"));
					return;
				}
				Restores.Add(MoveTemp(Read));
			}

			/** DNS7001: every cut falls where a mask is at full. */
			void CheckCuts()
			{
				for (const FReadEntry& Cut : Timed)
				{
					if (Cut.Entry.Verb != EDnaStageVerb::Cut || Cut.Entry.bFlag)
					{
						continue;
					}
					const bool bCovered = Timed.ContainsByPredicate([&Cut](const FReadEntry& Mask)
					{
						return Mask.Entry.Track == EDnaStageTrack::Mask && ::DreamNarrative::GetDnaMaskEnvelope(Mask.Entry, Cut.Entry.Start) >= 0.999f;
					});
					if (!bCovered)
					{
						Fail(TEXT("DNS7001"), Cut.Span, FText::Format(LOCTEXT("CutUncovered",
							"The cut to '{0}' is not hidden: no mask is at full at that moment (a distort or flash peaks at the middle of its range; a fade is full after its fade). A cut meant to be seen is written `cut -> {0} hard`"),
							FText::FromName(Cut.Entry.Target)));
					}
				}
			}

			bool PutBackLater(TFunctionRef<bool(const FDnaStageEntry&)> PutsBack, float After) const
			{
				return Timed.ContainsByPredicate([&](const FReadEntry& Each) { return Each.Entry.Start >= After && PutsBack(Each.Entry); })
					|| Restores.ContainsByPredicate([&](const FReadEntry& Each) { return PutsBack(Each.Entry); });
			}

			/** DNS7003: what a moment changes for good is put back, later in its track or in `restore`. */
			void CheckPutBack()
			{
				const FReadEntry* FirstTake = nullptr;
				float LastTake = -1.f;
				for (const FReadEntry& Each : Timed)
				{
					const FDnaStageEntry& Entry = Each.Entry;
					switch (Entry.Verb)
					{
					case EDnaStageVerb::BlendFromGame:
					case EDnaStageVerb::Cut:
					case EDnaStageVerb::BlendTo:
					case EDnaStageVerb::Whip:
						FirstTake = FirstTake ? FirstTake : &Each;
						LastTake = Entry.Start;
						break;
					case EDnaStageVerb::HideHud:
						if (!Entry.bRange && !PutBackLater([](const FDnaStageEntry& E) { return E.Verb == EDnaStageVerb::ShowHud; }, Entry.Start))
						{
							Fail(TEXT("DNS7003"), Each.Span, LOCTEXT("HudNotBack", "The HUD is hidden and never shown again: add `ui show hud` later in the track or under `restore`"));
						}
						break;
					case EDnaStageVerb::Text:
						if (!Entry.bRange && !PutBackLater([](const FDnaStageEntry& E) { return E.Verb == EDnaStageVerb::ClearText; }, Entry.Start))
						{
							Fail(TEXT("DNS7003"), Each.Span, LOCTEXT("TextNotBack", "The text stays up: add `ui clear` later in the track or under `restore`, or give it a range"));
						}
						break;
					case EDnaStageVerb::RampDark:
						if (!PutBackLater([](const FDnaStageEntry& E) { return E.Verb == EDnaStageVerb::RestoreLight; }, Entry.Start))
						{
							Fail(TEXT("DNS7003"), Each.Span, LOCTEXT("LightNotBack", "The lights go down and never come back: add `light restore 时长` later in the track or under `restore`"));
						}
						break;
					case EDnaStageVerb::KeyLight:
						if (Entry.bFlag && !Entry.bRange && !PutBackLater([&Entry](const FDnaStageEntry& E) { return E.Verb == EDnaStageVerb::KeyLight && !E.bFlag && E.Target == Entry.Target; }, Entry.Start))
						{
							Fail(TEXT("DNS7003"), Each.Span, FText::Format(LOCTEXT("KeyNotBack", "The key light on '{0}' stays on: add `light key {0} off` later or under `restore`, or give it a range"),
								FText::FromName(Entry.Target)));
						}
						break;
					default:
						break;
					}
				}
				if (FirstTake && !PutBackLater([](const FDnaStageEntry& E) { return E.Verb == EDnaStageVerb::BlendBack; }, LastTake))
				{
					Fail(TEXT("DNS7003"), FirstTake->Span, LOCTEXT("CameraNotBack",
						"The stage takes the camera and never gives it back: add `blend back 时长` after its last move, or `camera blend back 时长` under `restore`"));
				}
			}
		};
	}

	bool CompileDnaStage(const FDnaLineNode& Root, FDnaStageData& Out, FDnaDiagnosticSink& Diagnostics)
	{
		FStageReader Reader(Out, Diagnostics);
		return Reader.Read(Root);
	}
}

#undef LOCTEXT_NAMESPACE
