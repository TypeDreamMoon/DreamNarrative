---
title: Expressions
description: Expressions supported in conditions and interpolation.
---

| Syntax | Meaning |
| --- | --- |
| `and`, `or`, `not` | Logic |
| `==`, `!=`, `<`, `<=`, `>`, `>=` | Comparison; tags support equality, not ordering |
| `+`, `-`, `*`, `/`, `%` | Arithmetic |
| `心情 in (平静, 不安)` | Tag membership in a set |
| `chapter == 风暴` | Current chapter |
| `visited(老周_初见)` | Number of departures from this dialogue |
| `random(1, 6)`, `chance(0.3)` | Seeded, replayable randomness |
| `修灯.active`, `修灯.completed`, `修灯.failed`, `修灯.available` | Quest state |
| `风暴逼近.等级` | A parameter of the event that opened this dialogue |

There are no strings or ternary operators. Types are checked at compile time
([DNS4xxx](../diagnostics/dns4xxx.md)). Chinese names here are valid example identifiers.
