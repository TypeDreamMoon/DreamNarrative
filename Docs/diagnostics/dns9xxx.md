# DNS9xxx — Toolchain

## DNS9002

A source file cannot be read.

**Explanation:** The supplied path does not exist or is not readable. Check whether it is relative to
the project directory, or supply an absolute path.

## DNS9101

A test's `set` writes a story-owned fact.

**Explanation:** Test `set` simulates game code, which can write only `extern` facts. Story-owned state
must be changed through the story. For an initial condition, such as a preexisting relationship value,
put the write in `given`, which initializes state with story authority. If the game truly supplies
the value, add `extern` to the declaration.

## DNS9201

Exploration was not exhaustive.

**Explanation:** Exploration stopped at 100,000 states, `depth` (40 steps by default), or 60 seconds,
with unexplored states remaining. It does not count as a pass. A common cause is an ever-growing
number used in arithmetic, such as `% 2`, rather than only constant comparisons; the explorer cannot
cap it, so each increment adds a state. Increase `depth`, use only constant comparisons for that
number, or advance the exploration start with setup steps.
