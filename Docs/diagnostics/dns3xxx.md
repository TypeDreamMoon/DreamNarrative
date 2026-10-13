# DNS3xxx — Names and references

## DNS3001

A reference names an undeclared fact.

**Explanation:** Names used in conditions, `set`, `implied` and objectives must be declared with `fact`.
Check spelling, missing declarations and group prefixes: `墨梅册.第一页` cannot be shortened to `第一页`.
Explicit declarations are what make reachability and never-written checks possible.

## DNS3002

The speaker is not a declared character.

**Explanation:** A `Name: Speech` line requires a `character Name` declaration. Add it if a character
is speaking. For narration containing a colon, such as “Attention: the door opened”, prefix the line
with `> `. The parser deliberately avoids silently treating unknown speakers as narration so it can
catch spelling mistakes.

## DNS3003

A jump or `visited()` targets a missing dialogue.

**Explanation:** Names in `-> Name` and `visited(Name)` must identify a `dialogue` block. Renames that
leave old references are a common cause. Add `was OldName` to a renamed declaration so the validator
can report remaining old-name references.

## DNS3004

A name is declared twice.

**Explanation:** Named declarations share a project-wide namespace, including across files. The message
identifies the earlier declaration. Rename one of them. Duplicate parameter names within one event
also use this code.

## DNS3005

A reference names a missing quest.

**Explanation:** `accept`, `complete`, `fail`, `subquest` and expressions such as `Quest.completed`
require a `quest` declaration. Correct the spelling or supply the declaration.

## DNS3006

A reference names a missing stage.

**Explanation:** `play Name` and a trailing `@Name` must identify a `stage` block. Correct the spelling
or supply the declaration.

## DNS3007

The old name in `was` is still declared.

**Explanation:** `was OldName` migrates saves and references from a previous name. If that old name
still has a declaration, there are two objects rather than one renamed object, so redirects conflict.
Remove the old declaration or remove `was`.

## DNS3008

A reserved word is used as a name.

**Explanation:** Words such as `set`, `if`, `title` and `camera` have language meanings and cannot name
a character, fact or node. Use a different name. Chinese names do not collide with the English keywords.

## DNS3009

A reference names a missing chapter.

**Explanation:** `advance Name`, `advance -> Name`, `chapter == Name` and `chapter in (...)` require a
`chapter` declaration. Correct the spelling or add the declaration.

## DNS3010

A reference names an undeclared channel.

**Explanation:** Dialogue `channel Name` suffixes and channel `pauses` / `stops` lists must name declared
channels. Built-in `main` needs no declaration. Correct the name or declare it. At run time, an unknown
channel falls back to `main` with a warning.

## DNS3011

A reference names an undeclared event.

**Explanation:** `on Event` requires an `event` declaration. An `emit Event(Parameter = Value)` carrying
values also needs declared parameters so names and types can be checked. A bare `emit Name` carrying
no values and intended only for the game need not be declared.

## DNS3012

An event parameter is read outside the dialogue that event opened.

**Explanation:** `Event.Parameter` belongs to that invocation and is available in its entry condition,
line interpolation and `set` expressions. A dialogue reached through `->`, a dialogue opened by
another event, quests and chapters cannot read it. Store a value in a fact within the entry dialogue
if it must survive.

## DNS3013

An event has no parameter with this name.

**Explanation:** Names in `Event.Parameter` and `emit Event(Parameter = Value)` must appear in the
`event` block. Correct the spelling or add the parameter declaration.

## DNS3014

A test references a missing archive.

**Explanation:** `archive Name: Text` and `archive Name = Count` require an `archive` block. Names may
contain dots, such as `武器故事.白之约定`. Correct the spelling or add the declaration.
