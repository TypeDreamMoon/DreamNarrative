// Copyright DreamMoon. All Rights Reserved.
//
// An editing session over one source file: what the story view does to the text. Each operation is
// a few line edits through the printer, so the file changes only where the operation says -- adding
// a dialogue adds its lines, changing a condition rewrites one header line, connecting two dialogues
// adds one `->` line -- and everything else stays byte for byte. After each operation the text is
// parsed again; an operation that would leave a parse error the text did not have is refused.
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
