# Diagnostics

| Range | Owner | Description |
| --- | --- | --- |
| 1000–1999 | Lexer | Invalid characters, unterminated strings, inconsistent indentation |
| 2000–2999 | Parser | Parse failures, unexpected tokens, invalid block structure |
| 3000–3999 | Names and references | Undeclared facts, missing nodes, duplicate IDs, missing jump targets |
| 4000–4999 | Expressions and types | Type mismatches, undefined operations, incomparable values |
| 5000–5999 | Graph and reachability | Dead links, unreachable branches or chapters, incomplete clue chains, orphaned content |
| 6000–6999 | Localization and assets | Duplicate localization keys, missing voice references, missing assets |
| 7000–7999 | Stage | Track timing outside bounds, unmasked camera cuts, missing restoration |
| 8000–8999 | Generation and write-back | Asset generation failure, source conflicts, round-trip differences |
| 9000–9999 | Toolchain | Command-line arguments, file access, watcher and multiple-editor conflicts |

## Read the codes

- [DNS1xxx — Lexer](dns1xxx.md)
- [DNS2xxx — Parser](dns2xxx.md)
- [DNS3xxx — Names and references](dns3xxx.md)
- [DNS4xxx — Expressions and types](dns4xxx.md)
- [DNS5xxx — Graph and reachability](dns5xxx.md)
- [DNS6xxx — Localization and assets](reference.md#localization-and-assets)
- [DNS7xxx — Stage](dns7xxx.md)
- [DNS8xxx — Generation and write-back](dns8xxx.md)
- [DNS9xxx — Toolchain](dns9xxx.md)
