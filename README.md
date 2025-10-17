# LSML
"Listed Sections Markup Language"

> Mission: clear configuration files for end-users of software.

Practical for small, self-contained configuration files to be read and edited by anyone.

Objectives:
- Be obvious
    - There is only one to do things\*
    - Warn user clearly when they make a mistake
- Be concise
    - Minimal boilerplate syntax
- Be resiliant
    - Improper syntax in one line does not impact structure of other lines
    - Recover usable data from syntax errors when possible
- Be portable
    - Data is stored in hashmaps and arrays, structures available in nearly every programming language
    - Simple syntax for easier implementation
    - Define parsing behavior in a [spec](SPEC.md) to make different implementations predictable

Not the goal:
- Storing highly nested data
- Serializing objects

LSML syntax is "flat", making small, self-contained sections easier to read
at the cost of making nested data harder to read.


## Synax

LSML essentially combines [INI](https://en.wikipedia.org/wiki/INI_file) and [CSV](https://en.wikipedia.org/wiki/Comma-separated_values),
inheriting their simplicity, readability, and utility.
LSML avoids their pitfalls by having a [spec](SPEC.md).

```lsml
{table section}
key = value # comment
"x+y=1" = 'y=1-x' # quoted strings avoid ambiguity
4.0+5e0 = 0x9 # all text is treated as a string, even if it looks like something else
`text in backticks\x3A\n` = `is escapable \U0001F60E`

[array section]
1, 2, 3, 4 # all values separated by commas or newlines
5, 6, 7, 8, # trailing comma is okay
  9   ,  10 # unquoted strings have whitespace trimmed

# Use an array section like CSV to store tabulated data
["playlist"]
title, artist, album
"We're Finally Landing", "Home", "Before The Night",
"Daybreak", "Overcrest", "Back Again"

# references allow for structure
{references}
table reference = {}"table section" # references are prefixed by {} or [], per the section type
array reference = []array section
# broken references can be checked with tools like a linter
nameless table reference = {}"" # the empty string is never a valid section name


{edge cases}
# no value after a key results in the value being the empty string
empty = 
# no key before an equals sign results in the key being the empty string
=nothing
# so this is valid, but not a good decision
=

# quoted strings can contain the other kind of quote
sarcasm = 'the airplane food was "delightful"'
# strings started unquoted remain unquoted, so they keep the quotes as-typed
not 'fine' = she said she was "fine"
# strings started quoted must remain quoted, text after the quote is cut off with a warning
cut off = "fine?" he asked # "he asked" is not included in the string
# quoted strings always end at the end of the line, forgetting the end quote raises a warning, but keeps the string
one line only = "i forgot the end quote

```

\*There is only one way to do things, unless having only one way makes LSML significantly less unusable.

### See LSML's design explained in the [design doc](DESIGN.md).


## Interpreting Values

Although LSML only stores strings, it is important to be able to convert them into common data types,
such as booleans, integers, and floats.

```lsml
{table section}
# all numerical values parse from the first non-whitespace character in the value string to the last valid digit character
integer-like = 1 or 0x1 or 0b1 or 0o0
float-like = 1.0 or +1.0e-0
still one = "    1L water"


# all boolean literals must be one of these, and must match EXACTLY: case sensitive, no whitespace, no other strings count!
bool = true or false or True or False or TRUE or FALSE
not a boolean = true false # because the string is "true false", which is none of the literals above.
```

Since all values are strings, they can be interpreted any way the user pleases,
but in the interest in keeping the most basic contents of LSML files consistent between applications,
the following extra features should be avoided, despite being possible to embed within LSML:
- Inline lists and tables: there should be one way to make lists and tables.
- No macro system: data should be as-written. If you need to generate LSML, it likely isn't the right tool for your use case.

