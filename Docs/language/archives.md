# Archives

```dna
archive 守夜日志 profile
  title: 守夜日志
  entry
    第一夜:灯坏了。
  entry if 灯修好了
    灯亮了。
```

An entry unlocks the first time its condition becomes true and remains unlocked afterward.
An entry's text is translated under `archive.<Archive>.entry<n>`, numbered from 0 by position;
`entry [if Condition] #id` names it `archive.<Archive>.<id>` instead, and `dnc freeze` writes the numbers in.
