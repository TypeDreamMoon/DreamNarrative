<a id="chapters-and-playthroughs"></a>

# 章节与周目

```dna
chapter 上岛
chapter 风暴 after 上岛
  implied
    见过老周 = true          // Facts supplied when jumping to or selecting this chapter
  advance -> 天亮 if 灯修好了
```

首次进入某个章节时会创建检查点。`SelectChapter` 会恢复检查点，同时保留 `progress` 事实和支线任务。`StartNewPlaythrough` 会清空存档槽并保留配置文件。
