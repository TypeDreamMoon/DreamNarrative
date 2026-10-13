# DNS1xxx — Lexer

## DNS1001

Indentation contains a full-width space (U+3000).

**Explanation:** A Chinese input method may insert a full-width space that looks almost like an ordinary
space but is not an indentation character. The line's nesting cannot be determined. Replace leading
full-width spaces with ordinary spaces; the editor offers a quick fix.

## DNS1002

Tabs and spaces are mixed in indentation.

**Explanation:** A file must use either spaces or tabs for indentation. Mixing them on one line, or
using a different character from other lines, triggers this code. Nesting counts characters, so visual
alignment can disagree with actual depth. Use one indentation character consistently.

## DNS1003

Indentation is not a multiple of the file's indentation unit.

**Explanation:** The first indented line establishes the unit, such as two spaces. Every later indent
must be a multiple of it; three spaces cannot unambiguously mean one or two levels. Add the missing
spaces or remove the extras.

## DNS1004

A string is not terminated.

**Explanation:** An opening `"…"`, `“…”` or `「…」` has no matching closing quote before the end of the
line. Strings cannot span lines. Add the closing quote and match the quote style: `「` closes with `」`.

## DNS1005

An unrecognized character appears.

**Explanation:** A command or expression contains a character the language does not recognize, often
a special symbol pasted from elsewhere. Dialogue and narration allow arbitrary characters; keywords,
conditions and modifiers do not. Remove the character or use its supported half-width equivalent.
