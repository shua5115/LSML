# LSML v0.1 Spec

Listed Sections Markup Language

Author: Shua Halle

Inspired by the TOML spec.

## Objectives

- Be obvious
    - One way of doing things
    - Content suggests interpretation
    - Warn user clearly when they make a mistake
- Be concise
    - Minimal boilerplate syntax
- Be resiliant
    - Improper syntax in one line does not impact structure of other lines
    - Recover usable data from syntax errors when possible
- Be portable
    - Data is stored in hashmaps and arrays, structures available in nearly every programming language
    - Simple syntax for easier implementation
    - Define parsing behavior in a spec to make different implementations predictable

## Terms

LSML Processor: the program used to interpret an LSML document


## Table of Contents
- [LSML Document](#lsml-document)
- [Whitespace](#whitespace)
- [Comments](#comments)
- [Errors](#errors)
- [Strings](#strings)
    - [Unquoted Strings](#unquoted-strings)
    - [Quoted Strings](#quoted-strings)
        - [Escape Sequences](#escape-sequences)
- [Section Headers](#section-headers)
- [Tables](#tables)
    - [Key-Value Pairs](#key-value-pairs)
- [Arrays](#arrays)
    - [Comma-Separated Rows](#comma-separated-rows)
- [Section References](#section-references)
- [Parse Errors](#parse-errors)
- [EBNF Grammar](#ebnf-grammar)

## LSML Document

An LSML document:
- Is case-sensitive
- Must be UTF-8 encoded
    - May contain zero-bytes
- Contains LF (`'\n'`) or CRLF (`'\r','\n'`) to separate lines

## Whitespace

Whitespace is any of the following characters:
- `' '`: space
- `'\t'`: tab

## Comments

A hash symbol marks the rest of the line as a comment, except when inside a string:
```lsml
# This is a full-line comment
{table} # This is a comment after a section header
key = "value" # This is a comment at the end of a line
data = "# This is not a comment"
```
Comments may contain any UTF-8 data, except they end once a `'\n'` is encountered.


## Errors

Errors described in this spec that can occur during parsing are called "parse errors".
Parse errors do not discard a partially parsed document, they each have ways to recover partial information and prevent mangling the rest of the document.
Parse errors will be introduced when relevant, denoted by "PARSE ERROR: description...", followed by a description of how data is recovered.
They will be summarized in the [Parse Errors](#parse-errors) section for reference.

Implementations may define additional errors.
Those errors shall not cause the partially-parsed document to be discarded, but may abort the parsing operation.


## Strings

All strings in LSML must be unquoted, quoted, or escaped.
Strings have an "end delimiter" from the context of the line containing them.
Possible end delimiters are:
`'\n', '}', ']', '=', ','`

Unquoted strings are regular text.
Quoted strings are text between single quotes `'''` or double quotes `'"'`.
Literal strings are text between backticks "\`".

There are no multiline strings in LSML. Alternatives are:
- Use an escaped string with `'\n'` escapes
- Create an array and reconstruct a multiline string by concatenating columns as separated by commas and rows as separated by newlines.

### Unquoted Strings

Unquoted strings may contain any UTF-8 data, and they end once a `'\n'`, `'#'`, or
the in-context end delimiter is encountered. The string may not contain the in-context end delimiter.

Unquoted strings are trimmed of whitespace on either end.

```lsml
{   table  } # the name is just "table" (end delimiter is '}')
    key= value  # the key is just "key" (end delimiter is '='), value is just "value" (end delimiter is '\n')
```

If a `'\n'` or `'#'` is reached *before* encountering the end delimiter, then PARSE ERROR: string missing end delimiter.
- The string is cut off before the `'\n'` or `'#'`.

### Quoted Strings

Quoted strings start with a single quote `'` or double quote `"` (the "quote delimiter")
and may contain any UTF-8 data, except a literal newline character.
Quoted strings end once the quote delimiter or a `\n` is encountered.

If a `'\n'` is reached *before* encountering the quote delimiter, then PARSE ERROR: string missing end quote.
- The string is cut off before the `'\n'`.

If there is any non-whitespace between the ending quote delimiter and the in-context end delimiter, then PARSE ERROR: text after end quote.
- The text in between the ending quote delimiter and the end delimiter is not stored.
- Example: `key = "value1" value2` value2 is not stored because it appears after the quoted string "value1" was closed.

### Escaped Strings

Escaped strings start with a backtick "\`" and may contain any UTF-8 data or C-style escape sequences.
Escaped strings end once another backtick or a `\n` is encountered.

If a `'\n'` is reached *before* encountering the backtick, then PARSE ERROR: string missing end quote.
- The string is cut off before the `'\n'`.

If there is any non-whitespace between the ending backtick and the in-context end delimiter, then PARSE ERROR: text after end quote.
- The text in between the ending backtick and the end delimiter is not stored.
- Example: "key = \`value1\` value2" value2 is not stored because it appears after the escaped string "value1" was closed.

#### Escape Sequences

Escaped strings may contain C-style escape sequences:
- Characters: `'\a', '\b', '\f', '\n', '\r', '\t', '\\', '\'', '\"', '\?'`
- Octal byte: `'\O', '\OO', '\OOO'`, where each digit 'O' is `0-7` inclusive
- Hex byte: `'\xHH'`, where each digit 'H' is `0-9` inclusive, `A-F` inclusive, or `a-f` inclusive.
- Unicode 2-byte codepoint: `\uHHHH`, where each digit 'H' is `0-9` inclusive, `A-F` inclusive, or `a-f` inclusive.
- Unicode 4-byte codepoint: `\UHHHHHHHH`, where each digit 'H' is `0-9` inclusive, `A-F` inclusive, or `a-f` inclusive.

Both Unicode codepoint escapes must contain a valid codepoint, such that the value in hex is within these (inclusive) ranges:
- `'\u0000'` to `'\u007F'`
- `'\u0080'` to `'\u07FF'`
- `'\u0800'` to `'\uFFFF'`
- `'\U010000'` to `'\U10FFFF'`

If any of the following escape sequences are malformed, or if a backslash is used without a valid escape sequence, then PARSE ERROR: invalid escape sequence
- The characters within the escape sequence shall be copied literally into the string, including the backslash.
- The following specific causes cause this error:
    - Octal byte: any value greater than `'\177'`, e.g. `'\777'`
    - Hex byte: missing the second hex character, e.g. `'\xA'`
    - Unicode codepoint: out of valid range, or incorrect number of hex characters

The character escape sequences map to the following hex values:
```
'\a' -> 0x07
'\b' -> 0x08
'\f' -> 0x0C
'\n' -> 0x0A
'\r' -> 0x0D
'\t' -> 0x09
'\v' -> 0x0B
'\\' -> 0x5C
'\'' -> 0x27
'\"' -> 0x22
'\`' -> 0x60
'\?' -> 0x3F
```


## Section Headers

A section header starts a new section.
If the first non-whitespace character in a line is `'{'` or `'['` (the "section delimiter"),
and the following character is *not* the closing section delimiter (`'}'` or `']'` respectively),
then the line is a section header, and the string enclosed within the section delimiters is the section name:

```lsml

{ table section header }

["array section header"]

```

When starting to parse a new line, the LSML processor shall check if the line starts a new section before attempting to interpret it any other way.


The end delimeter is `'}'` for a table section header's name and `']'` for an array section header name.
If the end delimiter is not encountered before an `'\n'` or `'#'`, then PARSE ERROR: section header unclosed
- The section name is cut off before the `'\n'` or `'#'`

If the section name has already been used in the document, then PARSE ERROR: section name reused
- The section containing the redundant name is *skipped*:
  any entries contained within it are not stored or checked for syntactic validity.

If the section name ends up parsing to an empty string, then PARSE ERROR: section name empty
- The section containing the empty name is skipped:
  any entries contained within it are not stored or checked for syntactic validity.

If there is non-whitespace between the closing section delimiter and the `'\n'` or `'#'`, then PARSE ERROR: text after section header
- The non-whitespace characters after the section header are skipped


## Tables

Tables are started with a section header in curley braces: `{ table }`.

They contain key-value pairs: `key = value`.

The pairs are not stored in any specific order.

### Key-Value Pairs

Key value pairs are two strings separated by an equals sign `'='`.

The key is a string with an end delimiter of `'='`.
If the `'='` is not encountered before a `'\n'` or `'#'`, then PARSE ERROR: table entry missing '='
- The entry is skipped: it is not stored and the following value is not checked for syntactic validity.

If the key matches an existing key in the table, then PARSE ERROR: table key reused
- The entry containing the redundant key is *skipped*:
  it is not stored and the following value is not checked for syntactic validity.

The value is a string with an end delimiter of `'\n'`.
This means a newline *must* be encountered after the string,
so text between an end quote and the newline is properly considered a "text after end quote" parse error.

Empty keys and values are valid:
```
{table}
empty value = # value is the empty string
= empty key # key is the empty string
{bad but valid}
= # both key and value are empty
```

## Arrays

Arrays are started with a section header in square brackets: `[ array ]`.

They contain comma-separated rows: `val1, val2, "val3", ...`.

```lsml
[array]
1,2,3,4
5,6,7,
8,"9"
10
```

When parsed, arrays must maintain both their 1D and 2D structure such that they can be indexed linearly, or in 2D.
Array indices start at zero.

In the above example, the following are expected results for accessing 1D and 2D indices:
```
[0] -> "1"
[6] -> "7"
[9] -> "10"

[0,0] -> "1"
[1,2] -> "7"
[3,0] -> "10"
```

### Comma-Separated Rows

Each element in each row is a string with an end delimiter of `','`.

The last element in the row must end with either `','`, `'\n'`, or `'#'`.
- The trailing comma in the row does *not* imply an empty string value at the end of the row
- There is no error for missing the end delimiter here, since the newline or comment signifies the end of the row


## Section References

All values in LSML are strings, but there is a special string syntax called a "section reference".
A section reference stores the name of another section in the document.

```lsml
{table}
link=[]array # references the following array sectioin

[array]
{}"table", # references the above table section
```

A section reference must start with `"{}"` or `"[]"` *unquoted*,
and is followed by a quoted or unquoted string with the appropriate end delimiter for the context.

A section reference is parsed into a regular string, such that the first two characters, `"{}"` or `"[]"`,
are concatenated with the remaining string contents.

Section references may refer to non-existent sections, including an impossible section with an empty string name:
- `{}` and `[]` are valid reference syntax, the section name is just empty.


## Parse Errors

This is a table of all parse errors, when they occur, and their consequences:

| Parse Error | Condition | Consequence |
| ----------- | --------- | ----------- |
| Missing end quote | A quoted string does not end with the proper quote delimiter | The string is cut off at the end of the line |
| Invalid escape | A quoted string contains an invalid escape sequence | The escape sequence is copied literally |
| Text outside section | Non-whitespace characters encountered before the start of the first section in the document | The characters are discarded |
| Text after end quote | Non-whitespace characters encountered after an end quote and before the end delimiter | The characters are discarded |
| Text after section header | Non-whitespace characters encountered after a section header | The characters are discarded |
| Section header unclosed | A section header is missing the ending section delimiter | The name is cut off at the end of the line |
| Section name empty | A section name is the empty string | The entire section is skipped |
| Section name reused | A section name matches an existing section | The entire section is skipped |
| Table key empty | A table key is the empty string | The entry is skipped |
| Table key reused | A table key matches an existing key | The entry is skipped |
| Table entry missing equals | A table key does not end with an equals sign | The entry is skipped |


## Interpreting Values

Many programmers expect access to specific native data types from markup files.
LSML processors must be able to parse the following data types from strings:
- integer
- float
- boolean
- LSML section reference

In addition, LSML Processors shall provide functions to parse arbitrary user-provided strings into these concrete data types.

Using alternative syntax for the boolean or section reference types is disallowed:
- An LSML Processor shall not provide an option to interpret
  strings other than those outlined in the [\#boolean](#booleans) section,
  since that is an alternative syntax.
- An LSML Processor shall not provide an option to interpret
  strings not prefixed with `"{}"` or `"[]"` as a valid section reference type.

Additional data types may be interpreted by LSML Processors in any way.

While allowed, it is discouraged to detect and convert strings to the integer, float, boolean, or reference data types automatically,
because the application developer would have less control over the interpretation of LSML documents, and the type conversion is useless
if the type is incorrect according to the developer.

When parsing one of the data types above, the LSML processor may return one of these errors:
| Value Error | Condition | Consequence |
| ----------- | --------- | ----------- |
| Null   | A null value was encountered | No value is returned |
| Format | The syntax of the value is incorrect | No value is returned |
| Range  | The value of an integer or float is out of representable range | The out of range value is clamped to the closest in-bounds value |



### Integers and Floats

The LSML processor must be able to parse integers up to 64 bits wide,
both unsigned and signed two's compliment,
in base 10, 16 (hexadecimal), 8 (octal), and 2 (binary).

The LSML processor must be able to parse floats in 32-bit and optionally in 64-bit IEEE format.

For this section, both integers and floats will be referred to as "numbers".

Numbers may be prefixed by whitespace.
If the first character after whitespace is a `'+'` or `'-'`,
then the number will be positive or negative respectively, if the data type permits.

To avoid confusing numbers with numbers within strings, the last valid character in the
parsed number must be the last character of the string. If not, this is considered ERROR: value format,
and the number is not returned.

The following is a table of different number parsing properties:
| Format Name | Base | Prefix | Digit Characters (inclusive ranges) | Examples |
| ----------- | ---- | ------ | ----------------------------------- | -------- |
| Decimal     | 10   |  None  | `'0'-'9'` | `128`, `-57000`, `+9999` |
| Hexadecimal | 16   | `"0x","0X"` | `'0'-'9', 'A'-'F', 'a'-'f'` | `0xA`, `-0xFFFF`, `+0xabcdef` |
| Octal       | 8    | `"0o","0O"` | `'0'-'7'` | `0o200` |
| Binary      | 2    | `"0b","0B"` | `'0'-'1'` | `0b11001100` |
| Floating    | 10   |  None  | `'0'-'9'`, `'.'`, `'E'`, `'e'`, `'+'`, `'-'` | `1.234`, `-.567`, `+89.`, `1e-3`, `-1.25E+3` |
| Infinity    | None |  None  | `"INF"` | `INF`, `+INF`, `-INF` |
| Not a Number | None | None  | `"NAN"` | `NAN`, `+NAN`, `-NAN` |

If the parsed number is out of range for the desired integer width or not representable as a float, then ERROR: value range,
and the number is clamped to the closest in-bounds value:
- For example, a 16-bit signed integer would clamp to the inclusive range `-32768` to `32767`.
- For example, a 32-bit unsigned integer would clamp to the inclusive range `0` to `4294967295`.

An integer parsed from the Floating format must round the number towards zero and then return ERROR: value range if the floating point value
conversion to an integer loses precision.
- For example, `1.8` rounds to `1` and returns the error, because casting `1` back to a float is not equal to `1.8`.
- For example, `255.0` rounds to `255` and doesn't error, because casting `255` back to a float still equals `255.0`.

An integer parsed from the Infinity format must return ERROR: value range in all cases,
and the resulting number is the appropriate bound for that integer type.
- For example, `+inf` 

A float parsed from the Floating format must return ERROR: value range if the value is greater than the largest
representable float, or smaller than the smallest representable float:
- For example, `1e999` would result in positive infinity and `-1e999` would result in negative infinity. Both cause ERROR: value range.
- However, a float very close to zero, such as `1e-999`, is simply rounded to 0 and does not cause an error.



### Float

Floats may contain underscores (`'_'`) as separators between digit characters.
If an underscore is not surrounded by two digit characters, then ERROR: value format, no number is returned.
These separators are for visual clarity and do not affect the value of the number.

### Booleans

Booleans can be either `true` or `false`.
The encoding of `true` or `false` depends on the LSML Processor.

`true` booleans must match one of these strings EXACTLY, in both length and content: `"true"`, `"True"`, `"TRUE"`
`false` booleans must match one of these strings EXACTLY, in both length and content: `"false"`, `"False"`, `"FALSE"`


## EBNF Grammar

A formal description of LSML's syntax is available as an [EBNF](https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form) file:

[LSML Grammar.ebnf](LSML%20Grammar.ebnf)

The file only describes valid syntax, it does not include the semantics of handling invalid syntax or interpreting values.