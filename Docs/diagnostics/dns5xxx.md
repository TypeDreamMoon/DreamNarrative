# DNS5xxx — Graph and reachability

## DNS5008

A choice group has no fallback choice.

**Explanation:** If every choice has `if` or `once`, all may disappear at the same time. Each group
must retain one unconditional, repeatable choice. If a group is nevertheless empty at run time, it is
skipped and execution continues after the group.

## DNS5101

An exploration step produced an error or warning.

**Explanation:** The explorer executes each action and the resulting speech on all channels: talking
to a character, starting a game-only dialogue, writing an extern fact as the game, or raising a
listened-to event. Game-thread errors and warnings count as problems, including rejected writes
(ownership, type or set membership), dialogue waiting without an active timer, and replay producing
different results with identical choices and random draws. Errors in exploration setup also count.
The report includes the step, a path from the initial state, and a pasteable regression test. Inspect
the original log: the story may write an invalid value, or a game-side ledger observer may be faulty.

## DNS5102

A dialogue cannot stop.

**Explanation:** A jump-only loop with neither speech nor choices exhausts the runner's 10,000-step
limit. A different loop may keep producing speech or waits without ever offering a choice or reaching
the end. The report identifies the dialogue and includes a reproducer. Add an exit choice or a `set`
that eventually makes the looping condition false.

## DNS5103

A dialogue has no exit path.

**Explanation:** After a choice group, every possible choice returns to that group or another closed
region, so the dialogue can never end. A typical dead end has only a choice leading back to itself.
The report identifies the dialogue and choice text; its test reaches the group. Add a choice that
reaches the end or jumps outside the closed region.

## DNS5104

Saving and restoring changes the ledger.

**Explanation:** On sampled states the explorer snapshots the ledger, serializes only SaveGame
properties, loads a fresh ledger and compares facts, chapter, visited counts and once records.
Differences indicate a runtime or snapshot defect, not an authoring mistake. The report lists the
differences; the attached test reaches that state and uses `save` to reproduce the problem.

## DNS5105

A softlock makes an initially reachable goal unreachable from a reachable state.

**Explanation:** A `softlock` target can be reached from the exploration start, but not from some
reachable state. All successors of that state were explored, so this is a proven lock. For example,
choosing “I'll think about it” may permanently remove the mayor's quest offer. The report gives the
closest such state and an `explore` block whose setup reaches it and whose `eventually Goal` assertion
then produces DNS5108. Usually the fix is a return entry, such as letting the mayor offer an available
but unaccepted quest again.

## DNS5106

A possible softlock was found (warning).

**Explanation:** As with DNS5105, a reachable state has not yielded the goal, but its successors were
not fully explored because a depth, state-count or time limit was reached. The lock is unproven and
usually accompanies DNS9201. Increase `depth` or remove an unbounded state dimension, then rerun.

## DNS5107

An `always` condition is false in a reachable state.

**Explanation:** `always Condition` requires the condition in every reachable state. The report gives
the nearest counterexample and its ledger values, with a test that reaches it and uses `expect`.
Conditions are checked between exploration steps, not at intermediate points inside a dialogue.

## DNS5108

An `eventually` condition was never true.

**Explanation:** No explored state satisfies `eventually Condition`, or the target of `softlock`.
With complete exploration the goal is unreachable. With DNS9201, it simply was not found within the
explored portion. Check whether dialogue entry conditions, choices and relevant extern values can
be satisfied together.

## DNS5110

Content was never reached (warning).

**Explanation:** A dialogue, line, choice or chapter never appeared during exploration. Its condition
may be impossible, for example depending on a fact nobody writes, or it may not be connected yet,
such as a chapter driven only by a quest. This warning is expected for unfinished content but useful
for content that should appear. With incomplete exploration, the list describes only the explored portion.
