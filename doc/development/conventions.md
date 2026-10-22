# Coding Conventions

There's numerous conventions which are not covered by the formatting but we
still aim to have some consistency.


## Naming

Concept            | Naming convention
------------------ | -----------------
Classes            | `PascalCase`
Enums              | `PascalCase`
Enum Values        | `PascalCase` or `UPPER_CASE`
Functions          | `snake_case`
Methods            | `snake_case`
Properties         | `snake_case`
Variables          | `snake_case`
Constants          | `UPPER_CASE`

### Event, EventData, Param, etc.

The name of the `Event`, `EventData`, `Param`, etc. should be the *same* as the
variable name. There are some exceptions to this rule if there's some
post-processing or adjustment to the value.


## Access

Unless a method is *required* to be public (i.e. used by caller), it should be
`private`. Similarly, it should be `protected` (C++ only) if it needs to be
used by derivatives.


## C++ Specific

In general, [C++ Core
Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) is a
good resource (especially `ES.*`), but not all rules are followed here.

### Naming

Concept            | Naming convention
------------------ | -----------------
Concepts           | `PascalCase`
Template args      | `UPPER_CASE` or `PascalCase`
Macros             | `UPPER_CASE` (or sometimes `snake_case`)
Constants          | `UPPER_CASE` (or sometimes `snake_case` for `constexpr`)

### Required Braces

If an `if` statement is is multiline in either its condition or body, braces
*should* be used.

### Prefer

#### `auto`

`auto` should be used whenever possible, it avoids repetition and makes future
changes/refactor easier.

#### `const` functions

As many functions should be `const` as possible to prevent "mutability"
propegation and to make it clear that a function does not change anything. For
functions which cache something, `mutable` or `const_cast` can be used.

#### Scoped enums

Scoped enums (i.e. `enum class`) should be used instead of plain `enum`, which
provide better scoping and do not require prefixing values by something.

#### References

References (i.e. `&`) should be used instead of pointers, unless `nullptr` is a
*valid* state.

#### Structs

Use structs instead of `std::tuple` for readability.

#### `std` (STL)

Use `std` instead of custom implementations or Boost whenever possible.

### Avoid

#### `using namespace`

`using namespace` pollutes the current namespace and can propegate it
(especially in headers). Instead you can do `using <namespace>::<member>` to
get a specific thing from the namespace.


## Python Specific

In general it should be "Pythonic" which somewhat means to follow agreed upon
community conventions for Python. Here's a general
[definition](https://blog.startifact.com/posts/what-is-pythonic/) and some
guiding [principles](https://peps.python.org/pep-0020/).

### Naming

Concept            | Naming convention
------------------ | -----------------
Private method     | _`snake_case`
Unused variable    | _`snake_case`

#### Common Names

Concept            | Name
------------------ | -----------------
All args           | `*args`
All keyword args   | `**kwargs`

### Prefer

#### List/Dict Comprehension

Instead of building lists/dicts via for-loops and appending, use list/dict comprehension.

#### Type-annotations

While type-annotations are not required, they are *preferred*, as they provide
more info to users, linters, and code completion.
