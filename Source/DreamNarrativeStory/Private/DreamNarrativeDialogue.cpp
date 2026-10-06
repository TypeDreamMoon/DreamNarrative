// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeDialogue.h"

FName DreamNarrative::MainChannelName()
{
	static const FName Main(TEXT("main"));
	return Main;
}

const FDnaDialogueNodeData* UDnaDialogueSection::FindNode(FName Name) const
{
	for (const FDnaDialogueNodeData& Node : Nodes)
	{
		if (Node.Name == Name)
		{
			return &Node;
		}
	}
	return nullptr;
}
