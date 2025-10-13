# LSML
"Listed Sections Markup Language"

> Mission: clear configuration files for end-users of software.

Practical for small, self-contained configuration files to be read and edited by anyone.

Objectives:
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
    - Define parsing behavior in a [spec](SPEC.md) to make different implementations predictable

Not the goal: Storing highly nested data
- LSML keeps data structures flat, making individual tables and arrays easier to read at the cost of making nested data harder to represent


## General Synax

```sh
{table section}
key = value # comment
"name"= 'joe \x4D\x41\115\101' # strings can be quoted, and can contain C-style escapes
4+5 = 6 # all text is just text, even if it looks like a number or something else

["array section"]
1, 2, 3, 4 # all values separated by commas or newlines
5, 6, 7, 8, # trailing comma is okay
"CSV is useful, right?" # commas inside quoted strings don't separate

{another table}
table reference = {}"table section" # references are prefixed by {} or [], per the section type
array reference = []array section # and are followed by the string which is the section name
# a valid LSML file can have broken references, they can be checked later
nameless table ref = {}"" # references an impossible nameless section

empty = # no value after a key results in the value being an empty string
sarcasm = 'the airplane food was "delightful"' # quoted strings can contain the other kind of quote
lie = she said she was "fine" # strings started unquoted remain unquoted, the quotes within this string are literal
cut off = "fine?" he said # strings started quoted must remain quoted: this text DOES NOT contain "he said", it is cut off after the end quote.
```

See the rationale behind LSML's design choices in the [design doc](DESIGN.md)

## Interpreting Values

Although LSML only stores information as strings, it is important to be able to convert them into common data types, such as
- Booleans
- Integers
- Floats
- 2D arrays
- References

The following outlines behavior that a LSML library should have when converting parsed strings to types and data structures:
```sh
{table section}
# parsing single values as common data types

# all numerical values should try to parse from the first non-whitespace character in the value string
integer = 1 or 0x1 or 0b1 or 0o0 # ints must be parsable from these notations
float = 1.0 or +1.0e-0 # floats must be parsable from these notations
still one = "    1L water" # any characters after the longest valid value are ignored.

# all boolean literals must be one of these, and must match EXACTLY (case sensitive, no whitespace). No other strings count!
bool = "true" or "false" or "True" or "False" or "TRUE" or "FALSE"
not a boolean = true false # because the string is "true false", which is none of the literals above.

[matrix?]
# arrays must be stored considering the 2D row and column structure
# and allow querying the array by row and column
1, 0, 0, 4
0, 1, 0, 4, 5
0, 0, 1, 4, 5, 6

{references}
good ref = {}table section # this is a good reference because there is a section with this name and type in this file
missing ref = []  "nonexistent" # this is a broken reference because no section has this name
wrong type = []table section # this is a broken reference because the type is wrong
not a ref = table section # this is not a reference because it is not prefixed with {} or []
# The LSML data processor should provide a tool to check for any missing references.
```

Since all values are strings, they can be interpreted any way the user pleases,
but in the interest in keeping the contents of LSML files consistent between applications,
the following extra features should be avoided, despite being possible to embed within LSML,
and possibly more convenient than the base LSML syntax:
- Inline lists and tables: there should be one way to make lists and tables.
- Using custom separators in array sections: there should be one way to make lists.
- Treating regular strings as references to sections: references should be explicit.
- A macro system for text substitution: data should be as-written.
- A macro system for manipulating nested data structures: other storage formats are better suited to represent them, so use them instead.


## For Implementers: Parse Errors

Parse errors in LSML do not necessarily abort parsing the rest of the file.
Each error has a resilient response.
Parse errors are accumulated during parsing, each associated with a line number.
The user or implementer can specify that they want to abort parsing when an error is encountered.

In general, if the intent is clear, data can be recovered from invalid syntax.
However, there are certain cases which do not make sense and their data must be discarded.
These are distinguished by:
- SOFT ERROR: an error which can easily be recovered, and available data is stored
- LOSSY ERROR: an error which results in data being discarded

```sh
key # LOSSY ERROR: text before the first section is discarded

{vals1}
key=value
key=value2 # LOSSY ERROR: keys must be unique within a table. Any keys which match a previous are not stored.
key3="value3 
#" SOFT ERROR: missing quote. Any string value which does not close the quote before the end of the line is still stored, but raises an error.
key value # LOSSY ERROR: table entries missing an '='. This is an error in case the '=' is forgotten, which is what happened here.
"key with no value" # LOSSY ERROR: table entries missing an '='. This entry is not stored.
"key4=value4
#" LOSSY ERROR: this is missing a quote, and as a result, the key goes to the end of the line.
# So, the table entry does not contain an '=', which is a LOSSY ERROR. This entry is not stored.
# This generates both errors for clarity.

[vals1] # LOSSY ERROR: section names must be unique. This entire section will not be saved, and does not need to be checked for syntax.
data # Because this section wasn't unique, this data isn't stored.
"data
#" Because this section is skipped, it is not necessary to raise the missing quote error for the above entry.
"[not a section]" # parser must still distinguish quoted text from the start of the next section

# SOFT ERROR: missing '}' Sections missing the ending delimiter are still saved, since the user intent is clear:
{vals2
key=value # still saved under vals2
quoted first = "once upon" a time # LOSSY ERROR: text after end quote, quoted string must not be followed by any more text

{""} # LOSSY ERROR: section name cannot be empty. This section is skipped.
{   } # LOSSY ERROR: section name cannot be empty. This section is skipped.
{ "section name" with extra text } # LOSSY ERROR: text after end quote, string must either be fully enclosed by quotes or not start with a quote.

# Sections with extraeneous information after the header are still stored, but raise a syntax error:
[vals3] stuff # SOFT ERROR: text after header. This section is still stored.
{ {this is not ok} } # SOFT ERROR: text after header. This unquoted section name is closed by the first '}' encountered, so there is an extra '}' at the end!

```

# WHY?

Aren't there enough markup languages already? Why make another?

My initial motivation was a self-assigned programming challenge to make an INI parser in C using a custom hashmap.
This scope creeped into using a custom allocator, the simplest one being a bump allocator.
After succeeding in making the hashmap, I was thinking about all the flaws of INI, and looked into how other markup
languages were designed.

Each markup language was designed for slightly different purposes, and usually had a tradeoff between three parties:
- The developer: writes the software that is being configured, and defines the configuration schema
- The basic user: the one editing small configs, does not have much technical experience in software or IT
- The sys admin: the one editing large configs, who lives and breathes IT

I'm evaluating these languages for the purpose of software configuration, so I consider:
- The developer's experience integrating the parser with their software
- The basic user's experience editing a few small configuration files
- The sys admin's experience editing many large configuration files

These are the conclusions I came to:


INI - Legacy Software
---
Benefits
- Good for the basic user: Extremely simple and forgiving syntax for small configs, with INI allowing a "fill-in-the-blanks" pattern

Tradeoff for the developer: Developer defines interpretation of values in text, they have full control, but this isn't very convenient for some

Drawbacks
- Bad for sys admin: No standard, so they have no way to know which dialect of INI is being used unless they deep dive into documentation
- Bad for the basic user: No standard, same impact, just on a smaller scale


TOML - Tom's not-so-Obvious Markup Language
---
Benefits
- Good for the basic user: Syntax is still simple vs. INI, but has some oddities with quotes
- Great for the developer: Accessible as a hashmap and/or array in whatever software they use
- Great for the sys admin: nested configs are a cinch, and the parser has a spec, allowing for confidence

Tradeoff for developer: user defines types of values implicitly, and parser decides what to do with values before giving developer access

Drawbacks
- Bad for basic user: unspecified values (`key=`) being disallowed making configuration templates less rich in information
- Bad for sys admin: verbosity can be hard to manage

YAML - Yet Another Markup Langugage
---
Benefits
- Good for the basic user: basic syntax is simple and reads like a to-do list, and has explicit types to guide them in the right direction
- Great for the developer: Accessible as a hashmap and/or array in whatever software they use
- Great for the sys admin: there is a spec

Tradeoff for developer: user defines types of values implicitly, and parser decides what to do with values before giving developer access

Drawbacks
- Bad for the basic user: YAML has a more complex type system and syntax compared to other markup languages
- Bad for the sys admin: indentation-based syntax makes modifying nested configs error-prone, and it doesn't read easily with large indents


JSON - JuSt Omit the trailing comma Now
---
Benefits
- Decent for the basic user: workable for small files, syntax is easy but has a few gotchas
- Out-freaking-standing for the developer: seamless integration with JavaScript, oh and all the other languages too

Tradeoff with types is the same as TOML and YAML

Drawbacks
- Bad for everyone: no comments make configs hard to document
- Bad for the sys admin: large JSON files are hard to read, even harder to modify


XML - When everything can be represented in XML... nothing will be.
---
Benefits
- Good for the developer: XML schema allows checking of complex data formats
- Great for the sys admin: XML schema with tooling allows for confident configuration of large systems

Drawbacks
- Hell for the basic user: syntax makes them never want to touch XML ever again
- Bad for the developer: XML format requires more checks than other markup langugages to navigate the parsed data, and the schema is often redundant with these checks



LSML - Listed Sunk Cost Fallacy
---
Benefits
- Good for the basic user: Simple and forgiving syntax for small configs, fill-in-the-blanks is still possible
- Good for the developer: there is a spec, but they still have the freedom to build on top of it

Tradeoff with types is similar to INI: developer controls how text is interpreted, BUT the spec defines special data formats which parse consistently

Drawbacks
- Lacking for sys admin: syntax is not made for deeply nested data


## TL;DR

LSML aims to be a slightly better INI, with most of the improvement to come from having a semblence of a specification.
It makes the same tradeoff with types as INI: making it the developer's problem to parse small strings into concrete values, like numbers.
But, it defines ways to make data structures by strictly defining the form of hashmaps, arrays, and references.
This way it can represent the same data structures as every other modern markup langugage
while keeping the same "everything is a list in a section" feel of INI.


Thank you for stopping by, I hope you enjoyed my naive evaluation of and contribution to the ecosystem of markup languages.