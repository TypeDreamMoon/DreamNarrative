# Chapters and playthroughs

```dna
chapter 上岛
chapter 风暴 after 上岛
  implied
    见过老周 = true          // Facts supplied when jumping to or selecting this chapter
  advance -> 天亮 if 灯修好了
```

Entering a chapter for the first time creates a checkpoint. `SelectChapter` restores the checkpoint
while retaining `progress` facts and side quests. `StartNewPlaythrough` clears the save slot and
preserves the profile.
