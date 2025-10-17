# Design of LSML

This is a supporting document to the spec to explain
the rationale behind LSML's design.

It is safe to say that LSML does not make any
breakthroughs in the design of configuration formats.
However, I am personally interested in the design considerations
that go into these "general-purpose" configuration languages,
so I made one to explore the design space.

LSML became more like INI and CSV
as its design was iterated, since
key-value pairs and tabulated data
can represent most of what people
need to work with in a simple, readable,
easily editable manner.

However, in this document I will also argue that
"simple" configuration languages, specifically
ones that are hand-written and human-read,
will all become less usable as system complexity grows.


## Table of Contents

- [Why create another markup language?](#why-create-another-markup-language)
- [Named section references](#named-section-references)
- [No required syntax typing](#no-required-syntax-typing)
- [Parse errors don't abort parsing](#parse-errors-dont-abort-parsing)
- [Key-Value Pairs](#key-value-pairs)
- [Arrays are CSV](#arrays-are-csv)
- [String syntax](#string-syntax)
- [Concrete types](#concrete-types)
- [Boolean syntax](#boolean-syntax)
- [Number syntax](#number-syntax)
- [Section reference syntax](#section-reference-syntax)
- [Validation using other LSML files](#validation-using-other-lsml-files)
- [Why a C implementation first?](#why-a-c-implementation-first)


# Why create another markup language?

Aren't there enough markup languages already?

The initial motivation for LSML was a minor frustration with using TOML
as build configuration for the Rust programming language.
The various hidden keys and sections in the config required sorting through
pages of documentation to figure out what was possible and valid in `cargo.toml`.

I don't mean to imply that Cargo's use of TOML was a bad decision, this is more a result of laziness on my part,
because I didn't want to spend a long time reading the [Cargo docs](https://doc.rust-lang.org/cargo/).
Part of my struggle was known unknowns: I knew that I didn't know a lot about the `cargo.toml` system,
but the default template from `cargo new` has minimal information to guide me through learning the system.
Cargo made the sensible decision to use TOML because of its well-defined spec and cross-platform implementations.
Nonetheless, after this experience, the seed was planted for thinking about possible ways to allow
new users to more easily acclimate to a configuration system.

I identified two kinds of information that are critical for learning the expectations (the "schema") of a configuration format:
- What should the structure of the file be?
- What keys are expected in key-value-pairs?

Of course, this list is not exhaustive.
While in most cases, one could use comments or external documentation to describe the schema,
comments tend become redundant once a user is familiar with the system,
and external documentation requires knowing what to search for,
which isn't ideal for inexperienced users.

A "self-documenting" configuration would be helpful to new users, since the values within the configuration
suggest what structures or key-value-pairs must be present. This information provides keywords users can
use to more efficiently learn the system.

A limitation of TOML that prevents the key-value pairs schema from being communicated without comments
is that "unspecified values" are not allowed:
```toml
# invalid!
nothing=
```
My understanding of TOML's reason for this is that all values in TOML must have a concrete type,
[defined by the syntax of the value itself](https://toml.io/en/v1.0.0#keyvalue-pair).
Because TOML doesn't have a "nothing" type, like "null" or "nil", it doesn't make sense to assign a type to an unspecified value.

Unspecified values are optional key-value-pairs that communicate to the user that the key *could* be used.
This solves the hidden-value problem I was facing with TOML.

INI, TOML's predecessor, allows unspecified keys and values.
Because INI treats all values as strings, an empty key or value is just an empty string, and the application handles that case.

Seemingly, the only reason TOML doesn't allow unspecified values, while INI does, is because TOML has a type system, while INI does not.
Furthermore, it seems the only reason TOML doesn't allow empty unquoted ("bare") keys is to be consistent with the fact that
unspecified values are invalid. Specifically, if there must be text to the right of the equals sign, then there must also be text to the left of it,
otherwise the rules are inconsistent.

Because LSML is moreso a direct extension of INI than TOML, it also treats all values as strings,
and has the consistent behavior of treating "nothing" as the empty string, allowing what TOML considers unspecified values and empty bare keys.

The structure of the configuration is another important part of the schema.
Both INI and TOML must use comments, external documentation, or pre-generated template files
to communicate which tables and arrays must be present.
However, there is a way to make nested structure self-documenting: named references.
If there was a special string value that referred to another section, including its type,
then that special value would communicate that the section is expected to exist with that name and type,
similar to the function of unspecified values.

A hypothetical build config template in LSML can demonstrate this:
```lsml
{package}
name=lsml
author= # empty, suggests it should be filled
# etc...
dependencies={}dependencies # references a table, suggests it should exist
tests={}tests # but I don't see a tests section...? I should look at the docs to see what should be in it!

{dependencies}
tokio=1.* # you can never escape it

```

Notice that the "tests" section does not exist,
even though there is a reference to it.
The reference to it implies that it *could* exist,
which is valuable information!
If the application requires it to exist,
then it can inform the user with an error when the file is parsed,
and linting tools can be used to detect broken references.
LSML doesn't consider broken references an error.
It's up to the system built on-top of LSML to decide whether broken references are bad or not.

My vision for LSML is when the user is given a template file to fill out,
the convention of including unspecified values and section references
can serve as *partial* documentation for the most important parts of a configuration schema.
This gives inexperienced users a starting point for understanding
deeper parts of the schema by searching using the keywords given by key and reference names.

Certain design decisions, such as using named references, have significant tradeoffs in other areas,
and they will be discussed later.


When developing LSML, I looked across the internet for discussions
about designing and comparing markup languages, and the following are relevant:
- [TOML - Hacker News](https://news.ycombinator.com/item?id=36018817)
- [I Really Love TOML Files - Reddit](https://www.reddit.com/r/rust/comments/m37zya/i_really_love_toml_files/)
- [An INI Critique of TOML - Reddit](https://www.reddit.com/r/C_Programming/comments/vmurxy/an_ini_critique_of_toml/)
- [Why does Cargo use TOML - Rust Lang](https://users.rust-lang.org/t/why-does-cargo-use-toml/3577)
- [Be Stricter About Lack of Null/None/Nil - TOML Github](https://github.com/toml-lang/toml/issues/975)
- [StrictYAML Design - Hitchdev](https://hitchdev.com/strictyaml/why/)
- [What is wrong with TOML? - Hacker News](https://news.ycombinator.com/item?id=37493964)


By reading these, you will come to find that my thought process ended up quite similar to
Colm O'Connor, StrictYAML's author. However, I decided not to go the route of
YAML's indentation-based structure for a controversial reason, and a true reason.
- ~~Controversial~~ Irrelevant reason: indentation only helps readability in certain situations, and invalid indentation is an annoying, easy-to-cause bug
- True reason: indentation is YAML's niche, and LSML copying it would make it too similar to StrictYAML, making LSML's existence more unjustified than it already is

Significant indentation makes YAML-like markup languages superior to INI, TOML, and LSML
for representing deep heirarchies of data when it comes to conciseness.
However, to avoid indentation-related configuration bugs, and moreso to cement a different niche for LSML,
LSML doesn't have significant indentation. As a consequence, LSML discourages highly nested configuration
by implicitly making nesting harder to write, and less readable. That is simply the tradeoff made here.


After reading these discussions, I also came to a wider conclusion about configuration languages:

All general-purpose, hand-written configuration formats suffer from the same fundamental issue:
Complexity is unavoidable, and at a certain threshold, configuration files become too complex
to manage without sufficiently complex tooling, regardless of syntax.
- YAML users praise its efficient syntax for nested data, but users with large configurations still suffer:
    - They make mistakes with editing at the wrong indentation
    - They lose track of what the current level of indentation means
- TOML users praise its efficient syntax for flat data, but users with large configurations still suffer:
    - The syntactic burden of repeating nested table names grows with the level of nesting
    - Users lose track of the structure of large files, since the definition of a nested table may be far away from its parent.
- LSML, since it is similar to TOML, will inheret the same kinds of complaints.

To "manage" complexity, people create abstractions by leveraging multi-file configurations, configuration generators, and macro systems.

Multi-file configurations leverage the tree structure of the file system itself to define a heirarchy between configuration files.
- These are only beneficial when the configured system is tree-like at its highest level.
- Example: Go compilation. Each folder is a Go module, and it expects certain files to be present in the folder.
    There is very little configuration that actually needs to be written in text.
- Example: Plex. Each folder is a (sub)category, and the names of leaf folders and files (and file metadata) contain the "configuration".

Configuration generators effectively use a separate programming language as the configuration,
  and the config files as a communication protocol between the generator and the program being configured.
- These are only beneficial if the people doing configuration are programmers.
- Example: CMake. CMake generates makefile or other types of build scripts which would otherwise be too complex to write by hand.
- Example: JavaScript. JavaScript generates JSON files to communicate with almost everything on the internet. To be fair, I'm stretching definitions here.

Macro systems effectively turn the configuration language into a programming language,
  but suffer from expressing behavior less clearly than an actual programming language.
- These are only used if the people doing configuration are masochists,
  or have no other choice because of [technical debt](https://en.wikipedia.org/wiki/Technical_debt).
- Example: Xacro for XML. Define XML templates that are instantiated with a certain XML tag.
  Must be run through an Xacro parser to expand the macros before being used by the actual program.
  This exists because URDF (robot 3D model XML) files in ROS are unmaintainable without it.
- Example: Jinja. This is a configurable templating engine that allows for embedding logic within another file,
  such as variables, conditionals, loops, and filters, then running it through a Jinja processor to generate the complete file.
  Huh. This one is also used for XML. Curious. I wonder what this says about XML.

None of these solutions are generalizable to every system, and suggest that general configuration languages which store static information
in text are limited in the complexity of the system they can configure. This doesn't mean that static, hand-written configuration files are pointless,
only that if the system you are configuring will possibly scale to a large size, then using such a configuration language
in the first place is a questionable decision. Creating a program which manages or abstracts configuration data would be a better decision 
if or when the system's complexity is high.

- Example: Zig. Zig's build system can scale to large sizes because it is based on a well-designed programming language,
  and properly abstracts build system primitives, like build targets and filesystem interaction, so you don't have to worry about minor details.
- Example: Oxygen for XML. This is an IDE for editing XML files which has many tools for common XML editing operations,
  making large XML configurations possible to create in a reasonable amount of time.
- Example: Graphical website editors, like Wix or Squarespace (not sponsored). They both provide a complex graphical UI which generates HTML pages.
  Hey, more tools to avoid writing XML! This XML language must REALLY suck on its own.

With all of that in mind, I hope you understand that none of this talk about configuration language design really matters,
since most systems in-practice are too complicated to be served by static, hand-written text files alone.
Most systems today prioritize creating a good graphical user interface which uses an efficient and fast data serialization format,
instead of trying to use a configuration format as both a user interface and data serialization.
In certain cases where text formats interact with logic, like build scripts or websites,
programming languages tend to be the right tool for the job. Another notable example of this is how
Javascript web frameworks are now used as the backbone of website front-ends, not HTML files with macros.

But, I wouldn't have reached that conclusion if I didn't try to make a configuration language myself.

I will finally continue on to discuss the specific syntax decisions of LSML.
I will mostly be comparing LSML to TOML and INI, since LSML's design decisions are mostly made in reference to them.


# Named section references

The decision to make references the primitive for nested data is mostly a dogmatic decision,
based on the design goal of "there should only be one way to do things" in LSML.

TOML has two ways of making tables, and two ways of making arrays:
- Tables can be created with an INI-style header or with an inline table.
- Arrays can be created inline in key-value pairs, or as an array of tables using a double square brackets header.

INI has it worse, with technically infinite ways to make tables and arrays, since there is no official spec.
- Arrays in INI are usually inline delimiter-separated values, spanning a single line.

LSML's references allow representing the same kinds of data structures as TOML, but has significant drawbacks:
- Every table and array must be given a name.
- That name must be typed at least twice: once to define the section, and more times to reference it.
- That name must be unique, since all sections in LSML are top-level.

That sounds really bad for a lot of use cases, and you would be right!
However, as argued above, if the syntactic burden of writing LSML section references becomes too great,
then LSML probably isn't the right solution to your configuration problems.
LSML is designed for configuration in the shape of arrays or tables, possibly with one level of nesting.

Though, section references do provide some advantages:
- Section references self-document that a section should, or could, exist, if provided in a template file.
- Tables and arrays are always started by a section header, reducing visual noise by making all arrays and tables easier to spot.

# No required syntax typing

LSML does not require parsing a file considering the concrete types of the values it contains,
such as converting numbers, booleans from strings like "4.2" and "true".
Instead, individual string values may be converted to those types after parsing,
giving developers full control of the interpretation of their configuration files.


This is the exact philosophy of INI and StrictYAML, and falls under the same dogma of
"there should only be one way to do things".
In this case, that "one way" is interpreted as not doing the same type-checking work twice.

When using ANY configuration file with software, the developer must
make their program check the concrete type of a value from the configuration:

With XML, the developer may use a XML schema validator to make sure the file is ok,
but then their programming language makes them do a type conversion anyways because XML
only stores string values. Two separate checks, the developer only controls one.

With TOML, the spec requires that concrete types follow a certain syntax during parsing,
then requires the developer to perform a type check of the parsed value to load it,
in addition to the check that the value exists. Three separate checks, the developer only controls two.

With LSML, the parser doesn't need to check any schema outside basic LSML syntax. It is up to the
developer to perform an existence check and type check. Two separate checks, the developer controls both.
No redundancy in this case.


With syntax typing, the configuration writer decides the types of elements,
the parser must do work to detect these types, and the developer
must also do work validating their responses with type checks.

Without syntax typing, the developer decides the types of elements, only needing to check once.
This makes the most sense considering they are writing the application to be configured.

See this [blog post](https://hitchdev.com/strictyaml/why/syntax-typing-bad/) for a more in-depth explanation. TL;DR:
- Syntax typing is not clear for non-technical users
- Syntax typing is redundant unless the markup schema is expressed within the markup file itself (e.g. XML schema or C header files)
- Syntax typing introduces more visual noise by requiring special syntax characters
- Syntax typing makes things harder to read for humans, but easier to read for computers

Making LSML easier for people is prioritized over making it easier for computers, so this is a clear decision:
LSML does not require syntax typing, and type conversions may be performed on-demand after parsing.

However, in the [Concrete Types](#concrete-types) section, I will explain why the
LSML spec still defines how concrete types should be parsed, whether it be during parsing or on-demand.


# Parse errors don't abort parsing

Another feature which gives developers more control is the option to
ignore parsing errors. Because LSML syntax is never multi-line,
it is possible to recover from localized syntax errors, and parse
the rest of the document normally. In some cases, the developer may
prefer this behavior and use a file despite it having parse errors,
and I see no reason to arbitrarily restrict their options. Including
this behavior in the spec guarantees that those reliant on the behavior
can trust that it will be available on any system.

However, in most cases, a syntax error is a good reason to throw away the
file and tell the user to fix the problem, so of course that is still an option.


# Key-Value Pairs

Key-value pairs are of the form `key = value`.

This matches many existing languages' syntax, including INI, TOML, Lua tables,
and most importantly, basic math that *everyone* has learned.
It is safe to say that anyone can understand what this means after a two-second explanation.
If not, may God help them.

# Arrays are CSV

Continuing with the dogma of "there should only be one way to do things,"
it was a tough decision for what that "one way" of making arrays should be.
Unlike key-value pairs, there are more variations of arrays in different languages,
but a common feature of most of them is separating values with a single character delimiter,
which is almost always a comma.

One goal of LSML is to be usable by non-technical people, and comma-separated values are easy to understand,
since they look like regular English.

You may be yelling at your screen that CSV is a cursed data format because of all the different dialects,
similar to INI, but that what the spec is for! It forces LSML parsers to use a specific variation of CSV that makes
the most sense given the other rules of LSML.

Because of the [string syntax](#string-syntax) trimming whitespace on unquoted strings,
whitespace between comma-separated values is also trimmed.

Because it is extremely useful to store tabulated data (spreadsheets) for many application,
parsed LSML must maintain the 2D structure of the array.

The only somewhat arbitrary decision here is that a trailing comma in a CSV row does NOT result in an empty string value
as the last element in the row:
```lsml
[array]
1,2,3,4,
```
This case is ambiguous, but there are arguments for the behavior of not ending with an empty string:
- It is easier to copy and paste array values, since the ending comma doesn't matter
- Forgetting the extra comma (if it were significant) is an easy-to-miss error
- Convention: many modern programming languages support trailing commas in arrays, and in those cases the trailing comma doesn't imply a null/None value at the end.
- If the writer wanted the empty string to be at the end, they could be explicit and write `""`.


# String syntax

I'll start with the most spicy decision: LSML has no multiline strings.

Multiline strings introduce problems which would make LSML harder to read:
long multiline strings make it harder to tell what is LSML and what is not,
and especialy hard to keep track of assicated key-value pairs, or array rows and columns.

Multiline strings also have a delimiter problem: what delimiter would you use?
Whatever delimiter you choose cannot be used in other parts of LSML syntax, nor inside the string.
No matter what you choose, multiline strings require some workaround to not collide the delimiters with data
inside the string, whether it's TOML's escaping triple quotes, Lua's arbitrary length delimiters, Go's backtick, or C++'s user-defined delimiters.


The main applications of multiline strings are
- Storing large blocks of text or binary data (serialization)
- Embedding other language syntaxes in the document (also serialization)

Since LSML is not for serialization, it's omits multiline strings for clearer, line-by-line syntax.

The next, less-spicy take is that escaped strings use backticks as delimiters.
Non-technical users don't know what an escape sequence is, so it would be confusing to
make either single or double quoted strings have the ability to use escape sequences.
Furthermore, the single and double quotes look almost identical, and having them behave differently
is simply not obvious. However, backticks look similar enough to quotes, but require using an unusual part
of the keyboard, making it slightly clearer that this string does weird things.

The dogma of "there should only be one way to do things" conflicts with how many ways there are to make strings,
There are multiple ways to make strings because many users expect those types of strings to exist:
Quoted strings are needed to separate strings from surrounding syntax, like an '=' in key-value pairs,
and escaped strings are for users who need to store a newline or arbitrary byte data.


If you *really* want to use LSML for serialization, your best bet is to use an escaped string
and fill it with escape characters when necessary to avoid closing the string.
This is possible if you just replace all backticks and newlines with their escape sequences in the string.
This is not hard with software tools, and if you are doing serialization, you are probably a technical user,
and so you probably have access to or can create a tool with this simple capability.


# Concrete types

Although in the [no syntax typing](#no-syntax-typing) section there is an argument
made for developers being in control of how values are interpreted,
there is also a strong argument that there should be a canonical interpretation of
certain types of values.

INI and CSV both suffer from not formally describing how values should be parsed,
and it would be foolish to do the same with LSML, which tries to combine them.

My clever trick for maintaining developer control while enforcing a
common concrete type syntax comes from the draft spec:

> Many programmers expect access to specific native data types from markup files.
> LSML processors must be able to parse the following data types from strings:
> - integer
> - float
> - boolean
> - LSML section reference
> 
> In addition, LSML Processors shall provide functions to parse arbitrary user-provided strings into these concrete data types.
> 
> Using alternative syntax for the boolean or section reference types is disallowed: ...
> 
> Additional data types may be interpreted by LSML Processors in any way.

This maintains the separation between parsing an LSML document and parsing strings into concrete data types,
giving developers full control over parsing operations,
while also defining exactly how common native data types are parsed so behavior is consistent between implementations,
giving developers the confidence that implementations will behave consistently.

I will note that for some programming languages, syntax typing is relatively
easy to implement and the burden of type checking doesn't outweight the convenience
of values already being converted to the right type.
So, implementations are allowed to use syntax typing, but
they must still parse booleans, integers, floats, and section references per the spec.


# Boolean syntax

Booleans are `"true"`, `"True"`, `"TRUE"`, or `"false"`, `"False"`, `"FALSE"`.

This matches forms of boolean literals in common programming languages.
While this goes against the dogma of "there should only be one way to do things",
non-technical users would find it confusing that one case-sensitive version would be more preferred than another.
For everyone, it would lead to small, easy-to-miss typos causing large errors.
Even different technical users familiar with differing conventions for booleans benefit from LSML being less opinionated here,
since they can keep doing what is natural for them.
It is another small deviation from the dogma for a usability win.


# Number syntax

I won't restate how numbers are parsed as integers or floats from the spec, it's pretty complicated.
But, it's complicated for a reason: the non-technical configuration writers expect that the value they write will be used directly.
However, application developers have complete control over the size and type of the number in use in their software.

I'll illustrate my point with an example:
- The user writes a value as `1e3` for a quantity.
- The developer wants this quantity as an integer.
- The C standard library function `atoi` returns 1, not 1000, since `'e'` is not an integer digit, so the parsing stops there.
- The user is confused why their quantity is 1 when they wrote 1000 in valid scientific notation.
- So, to make the value what they expect, the value must be parsed as a float, then converted to an integer in this case.

And another example:
- The user writes a value as `0o177` for a quantity.
- The developer wants this quantity as a float.
- The C standard library function `atof` returns 0, not 255, since `'o'` is not a valid float character, so the parsing stops there.
- The user is confused why their quantity is 0 when they wrote the equivalent of 255 in octal.
- So, to make the value what they expect, the value must be parsed as an integer, then converted to a float in this case.

In general, values prefixed with `"0x"`, `"0o"`, or `"0b"` are always parsed as integers then converted to the correct type,
and values without those prefixes are parsed as the intended type first, but if they end with `'.'` or `'e'` they are parsed as floats.
In all cases, the number is *clamped* to the correct range, avoiding any integer overflow.

This logic sidesteps issues with syntax typing, because it decouples the intentions of the user and developer,
producing a numeric value closest to what the user intended in the type that the developer wants.


# Section reference syntax

Section references are prefixed with `"{}"` or `"[]"`, followed by any type of string,
then the prefix and parsed string are concatenated.

The reason for the prefix is to differentiate section references from section headers.
Section headers may not have an empty name, so section reference prefixes are free to use
the section delimiters side-by-side unambiguously.

This has an additional advantage specifically for a C implementation,
since a section name can be extracted from a parsed section reference without any allocations.

This simplicity is present in other languages where taking the substring of a string is a trivial operation.


# Why a C implementation?

Making a working parser in C proves a point and can kickstart adoption:
C is widely supported, and most of C's features are available in other programming languages,
so if an implementation can be made in C, it should be possible and likely easier to do so in other languages.
If the C implementation is portable enough, a parser will already be available for all devices
and software which can interface with a C library.

Though, in all honesty, it was a personal programming challenge.

