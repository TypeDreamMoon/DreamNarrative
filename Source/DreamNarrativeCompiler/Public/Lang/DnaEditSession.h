// Copyright DreamMoon. All Rights Reserved.
//
// An editing session over one source file: what the story view does to the text. Each operation is
// a few line edits through the printer, so the file changes only where the operation says -- adding
// a dialogue adds its lines, changing a condition rewrites one header line, connecting two dialogues
// adds one `->` line, dragging a stage's entry rewrites the time on its one line -- and everything else
// stays byte for byte. After each operation the text is parsed again; an operation that would leave a
// parse error the text did not have is refused.
//
// Undo and redo are the session's own: a stack of texts, not editor transactions.

#pragma once

#include "CoreMinimal.h"
#include "Lang/DnaSyntax.h"

namespace UE::DreamNarrative::Lang
{
	class FDnaSourceEdit;

	class DREAMNARRATIVECOMPILER_API FDnaEditSession
	{
	public:
		/** Starts editing a file whose text is InText (what is on disk, or an unsaved version of it). */
		void Open(const FString& InPath, const FString& InText);

		const FString& GetPath() const { return Path; }
		const FString& GetText() const { return Text; }
		const FDnaDocument& GetDocument() const { return *Document; }

		/** True when the text differs from what Open was given or what was last marked saved. */
		bool IsDirty() const { return Text != SavedText; }
		void MarkSaved() { SavedText = Text; }

		/**
		 * Writes the text. Refuses, keeping the edits, when the file on disk changed since Open or the
		 * last save -- unless bOverwriteExternal, the user's explicit choice to replace that change.
		 */
		bool Save(FText& OutError, bool bOverwriteExternal = false);

		/** Whether Save would write: the file reads and still holds what Open read or the last save wrote. */
		bool CanSave(FText& OutError) const;

		/** True when the file on disk no longer holds what Open was given or the last save wrote. */
		bool IsChangedOnDisk() const;

		/** Drops every unsaved edit and the undo history and reads the file again. */
		bool Revert(FText& OutError);

		// -------------------------------------------------------------- operations

		/**
		 * Adds `dialogue Name Clauses` (`for 甲 if 熟`, `on 事件 channel 侦察`, ...) with Lines as its body,
		 * after the last declaration of the file.
		 */
		bool AddDialogue(const FString& Name, const FString& Clauses, const TArray<FString>& Lines, FText& OutError);

		/** Rewrites a dialogue's `if`: Condition as written, or empty to drop it. The rest of the header stays as it is. */
		bool SetDialogueCondition(const FString& Dialogue, const FString& Condition, FText& OutError);

		/** Adds `-> To` as the last line of From's body. */
		bool AddGoTo(const FString& From, const FString& To, FText& OutError);

		/** Removes a declaration and every line under it. */
		bool RemoveDeclaration(EDnaLineKind Kind, const FString& Name, FText& OutError);

		/** Replaces the content of one line of the current text. */
		bool SetLine(int32 Line, const FString& Content, FText& OutError);

		// -------------------------------------------------------------- stages

		/**
		 * Rewrites when the stage entry on Line happens: Start, and End for a range (End is ignored for a
		 * point). Times are in the stage's unit (seconds, or a fraction of the line) and are written the
		 * way the stage writes them; the rest of the line stays, the verb in its column when there is room.
		 */
		bool SetStageEntryTime(int32 Line, double Start, double End, FText& OutError);

		/** Rewrites how long the entry on Line takes to blend, fade, ramp or push, in the stage's unit. */
		bool SetStageEntrySpan(int32 Line, double Over, FText& OutError);

		/** Adds `时间 VerbAndArgs` as the last entry of a stage's track, adding the track when it has none. */
		bool AddStageEntry(const FString& Stage, const FString& Track, double Start, double End, bool bRange, const FString& VerbAndArgs, FText& OutError);

		/** Removes the entry on Line, and its track's line when it was the track's last entry. */
		bool RemoveStageEntry(int32 Line, FText& OutError);

		/**
		 * Switches a stage between counting in seconds and following its line. LineSeconds is the length
		 * of line the times are converted against; the switch to seconds writes it as the duration.
		 */
		bool SetStageTimeBase(const FString& Stage, bool bLine, double LineSeconds, FText& OutError);

		/** Any change made through the printer, as one step: kept when it parses as cleanly as before. */
		bool ApplyEdit(TFunctionRef<bool(FDnaSourceEdit&, FText&)> Edit, FText& OutError) { return Apply(Edit, OutError); }

		/** The declaration of a kind and name at the top of the current text; null when there is none. */
		const FDnaLineNode* FindDeclaration(EDnaLineKind Kind, const FString& Name) const { return FindRoot(Kind, Name); }

		/** The node on a line of the current text; null when the line is blank, a comment or past the end. */
		const FDnaLineNode* FindLine(int32 Line) const;

		/** The stage declaration a line belongs to; null when it is not inside one. */
		const FDnaLineNode* FindStageOf(int32 Line) const;

		bool CanUndo() const { return Undos.Num() > 0; }
		bool CanRedo() const { return Redos.Num() > 0; }
		bool Undo();
		bool Redo();

		/** The parse errors the current text has, `file(line,column): DNSxxxx message` each. */
		TArray<FString> GetParseErrors() const;

	private:
		/** The declaration of a kind and name at the top of the current text; null when there is none. */
		const FDnaLineNode* FindRoot(EDnaLineKind Kind, const FString& Name) const;
		/** Runs Edit on a printer over the current text; keeps the result when it parses as cleanly as before. */
		bool Apply(TFunctionRef<bool(FDnaSourceEdit&, FText&)> Edit, FText& OutError);
		void Reparse();

		FString Path;
		FString Text;
		FString SavedText;
		TSharedPtr<FDnaDocument> Document;
		int32 ParseErrors = 0;
		TArray<FString> Undos;
		TArray<FString> Redos;
	};
}
