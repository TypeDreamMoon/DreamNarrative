// Copyright DreamMoon. All Rights Reserved.

#include "DreamNarrativeDialogue.h"

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
