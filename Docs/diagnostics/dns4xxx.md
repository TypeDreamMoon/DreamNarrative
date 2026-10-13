# DNS4xxx — Expressions and types

## DNS4001

Types do not match.

**Explanation:** Examples include assigning a number to bool, ordering a bool against a number, using
`+=` on a non-number, or passing non-conditions to `and`. The message reports the involved types.
Facts have only `bool`, `int`, `float` and `tag`; compare the expression against their declarations.

## DNS4002

A tag value is outside its declared set.

**Explanation:** A tag fact declares allowed members with `in (甲, 乙, 丙)`. Assignments, comparisons and
membership lists may use only those values. Correct a typo or add the intended member to the set.
Declaring a tag fact or event parameter without a set also triggers this code.

## DNS4003

A constant is outside the declared range.

**Explanation:** Assigning 99 to a fact declared `range 0..10` is a statically detectable error.
Runtime writes are clamped, but an out-of-range constant in source should be corrected.

## DNS4004

These values cannot be ordered.

**Explanation:** `<`, `<=`, `>` and `>=` apply to numbers. Tags and bools have no ordering. To check
whether a state has reached a particular value, use `==` or `in (...)`.

## DNS4005

A built-in function is used incorrectly.

**Explanation:** The built-ins are `visited(Dialogue)`, `random(Minimum, Maximum)` and
`chance(Probability)`. Wrong argument counts or types and unknown function names trigger this code.
Quest state uses a property such as `Quest.completed`, not a function call.

## DNS4006

A constant is required here.

**Explanation:** Fact defaults, event parameter defaults and chapter `implied` values are evaluated
at compile time and stored in assets. They cannot depend on facts, chapters or randomness. Use a
literal or set member.

## DNS4007

A gameplay source does not match the destination type.

**Explanation:** A tag-backed fact can be bool (tag presence) or int (tag count). `from magnitude`
provides a number and supports int or float. `from instigator tags` / `from target tags` supports bool
for presence or tag for the next segment beneath the specified tag: for `敌人.体型.大型`, that segment
is `大型`, which must belong to the parameter's set. Change the destination type or source.
