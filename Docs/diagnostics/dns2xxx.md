# DNS2xxx — Parser

## DNS2001

A line is indented under a line that does not open a block.

**Explanation:** Block headers such as `dialogue`, `if` and choices can have nested content. Leaf lines
such as `set`, `->` and properties cannot. Dedent the line or place it under its intended block header.
An indented first line also triggers this code. Dialogue and narration are exceptions: deeper plain
text lines continue the same spoken line.

## DNS2002

A block header has no body.

**Explanation:** Headers such as `if`, `else`, `journal`, `on accept` and track names require deeper
content. An empty block usually indicates missing text or wrong indentation. Supply the body or remove
the header. An empty choice, such as `- Goodbye` with no action, is permitted.

## DNS2003

A line is in the wrong context.

**Explanation:** Each block accepts specific line types: declarations at the top level, quest properties
inside `quest`, and tracks inside `stage`. This line does not fit its location, often because it lost
one indentation level. Check its indentation and keyword.

## DNS2004

A modifier occurs twice on one line.

**Explanation:** Each suffix can appear at most once: one `if`, one `once`, one `#id`. Repeating a suffix
would make precedence ambiguous. Keep only one.

## DNS2005

A choice appears outside dialogue.

**Explanation:** A line starting with `- ` is a player choice and belongs inside a `dialogue` block,
including its nested `if` or choice bodies. At the top level or inside a quest it usually means
indentation was lost. Indent it back into the dialogue.

## DNS2006

An ` if ` in dialogue was treated as ordinary text (information).

**Explanation:** A trailing `if` is a condition modifier only when the remainder parses as a condition
and references at least one declared name. Otherwise it remains part of the spoken text. If a condition
was intended, check declarations and expression syntax. If literal speech was intended, ignore this
message or quote the whole line to suppress it.

## DNS2007

The parser expected one thing and encountered another.

**Explanation:** This general syntax error covers a missing condition after `if`, an invalid type after
`fact Name:`, a missing duration after `wait`, unmatched parentheses and similar cases. The message
states what was expected and what was found. Supply or correct that part.

## DNS2008

Extra content follows a complete line or expression.

**Explanation:** The `2` in `set x = 1 2`, or trailing words after `-> Node`, have no syntactic role.
Check for a missing operator or text that belongs on the next line.

## DNS2009

This line type does not allow this modifier.

**Explanation:** A trailing `once` belongs to a choice; `once` on a dialogue header governs entry instead.
`for Duration` and `@Stage` belong to dialogue and narration lines. Misplaced modifiers are recorded but
do not take effect. Remove them or move them to a supported line.

## DNS2010

An `else` has no preceding `if`.

**Explanation:** `else` and `else if` must immediately follow an `if` or `else if` at the same depth.
An intervening spoken line or two consecutive `else` clauses break the chain. Reorder the lines or
move the intervening content into a branch.

## DNS2011

A quest's `priority:` must be main, side or hidden.

**Explanation:** `main` identifies a main quest, checked for softlocks by default; `side` identifies a
side quest; `hidden` is an internal bookkeeping quest such as a clue chain. Other words have no defined
behavior. Choose one of these three values.

## DNS2012

A quest's `timeout:` must be a duration.

**Explanation:** The timeout begins at acceptance. Write a positive number followed by its unit, such
as `20m`, `90s`, `1.5h` or `300ms`. It uses the narrative clock and stops while the game is paused.
Other forms, zero and negative values do not define a valid timeout.

## DNS2013

A quest `on` block cannot `wait`.

**Explanation:** Blocks such as `on accept` and `on complete` run synchronously when the quest changes;
there is no suspended continuation. Put actions requiring a wait in dialogue, or use `play` to request
a stage from the game.

## DNS2014

A quest can have at most 31 objectives.

**Explanation:** Latched objectives occupy bits of one integer. If a quest exceeds that capacity, split
it into `subquest` blocks with their own journals and rewards, which also makes progress clearer to players.

## DNS2015

A channel property is invalid.

**Explanation:** Channel policies include `input` (lock / free), `advance` (player / auto), `busy`
(queue / interrupt / drop), `stage` (pause / continue), and comma-separated `pauses` / `stops` lists. An unknown property, invalid
value or empty list triggers this code. A channel cannot pause or stop itself; `busy` governs its own
conflicts. Nor can it stop `main`, which belongs to the player's active conversation. To silence other
channels during a main conversation, declare `pauses` on `channel main` instead.

## DNS2016

`channel main` can only change `pauses` and `stops`.

**Explanation:** The main channel locks input, waits for the player, and replaces an old conversation
when a new one starts. Those are its defining policies. Declare another channel for free input and
automatic advancement, and assign dialogues with `channel Name`.

## DNS2017

A dialogue cannot be both a character entry (`for`) and an event entry (`on`).

**Explanation:** A character entry is selected when the player talks to that character; an event entry
is selected when an event is raised. Combining them makes the trigger ambiguous. Split them into two
dialogues and use `->` to share a third dialogue if needed.

## DNS2018

A dialogue on an automatic channel cannot have choices.

**Explanation:** An `advance: auto` channel advances by line duration and does not wait for player
selection. A choice would stall it indefinitely. Move interactive dialogue to an `advance: player`
channel, such as `main`, or remove the choices.

## DNS2019

Event parameters only support a type, set, range and default value.

**Explanation:** Parameters resemble facts, for example `数量: int range 1..9 = 1`, but have no saved
state, ownership or rename history. Fact-only modifiers such as `extern`, `session`, `profile`,
`progress`, `was` and `gameplay` do not belong on them. Remove those modifiers; declare a fact if a
value must be game-owned and persisted.

## DNS2020

An `emit` specifies the same parameter twice.

**Explanation:** `emit 发现敌人(数量 = 1, 数量 = 2)` does not identify which value to send. Supply each
parameter at most once. Omitted parameters use their declared defaults.

## DNS2021

A fact supplied by a gameplay tag must be `extern`.

**Explanation:** `fact Name: bool extern gameplay Tag` mirrors a tag from the player's ability system.
The GAS bridge writes on behalf of the game, so the story cannot own that fact. Add `extern`, or
remove `gameplay` if the story should control the value.

## DNS2022

A parameter has `from`, but its event has no `gameplay` mapping.

**Explanation:** `from magnitude` and `from target tags Tag` read a GameplayEvent payload. Such a
payload exists only for an event declared with `gameplay Tag` and raised through the bridge. Add the
event's gameplay tag, or remove `from` if only game code or the story raises it.

## DNS2023

A fact can occupy only one storage scope.

**Explanation:** The default is the save slot; `session` lasts only for the current session and is not
saved; `profile` has a separate snapshot spanning slots and playthroughs. These scopes are mutually
exclusive. `progress` is a subdivision of the save slot: chapter selection retains its value and
checkpoints omit it. It cannot combine with `session` or `profile`. Group modifiers apply to each
member, and conflicts are reported on the affected member.

## DNS2024

An archive can have at most 31 entries.

**Explanation:** Unlocked entries occupy bits of one integer. Split a larger archive by chapter,
weapon tier or another useful grouping so it fits the representation and is easier to browse.
