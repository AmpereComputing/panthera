# Parameters

Panthera supports parameters in the command-line or `-cfg` options. Parameters are _always_ hierarchical, but there are various methods of setting parameters within some hierarchy.

## Help & Listing Parameters

Using `-h` or `--help` will provide a complete listing of all parameters and their current values. For example:

    ./install/bin/amperesys -cfg ./install/config/model/sys/domino.cfg -h

Will show all available parameters for `domino.cfg` and their current values.

## Command-line

Parameters can be set from the command-line like:

    -<param> <value>
    -cfg <file>

Most syntax (as mentioned below) is supported via command-line after `-`; however, all parameters are in the root scope/hierarchy.

## Config Files

Config files allow parameters to be in a file and applied through `cfg`. Additionally, they allow configs to be associated with a "config scope" (see scoped versions of `cfg` options) which enable overriding these scopes to facilitate changing module types or other widespread changes.

### Unscoped

All parameters in the file will be inserted at the current scope/hierarchy.

    cfg "<file>"

### Scoped

If a scope is provided (string or regex), all parameters with the config file will be inserted at that scope/hierarchy (which is also relative to the current scope/hierarchy).

    <scope>:cfg "<file>"

The scope can be a singular string/hierarchy, or regex but *must not* already exist (see other forms below) unless it was the *same exact file* before. This *adds* a new config scope with parameters from the file specified, which can be overridden or appended to via the following options.

#### Overriding

In some cases (like overriding module types), you want to override the *entire* scope which was previously included with a prior `<scope>:cfg`. To override a scope you can use:

    <scope>:override-cfg "<file>"

This will effectively *remove* the parameters which were previously included in that scope with a prior `<scope>:cfg`. Note, that this will *require* a previous include on that config scope.

For example, in the following:

    foo:cfg "something.cfg"
    ...
    foo:override-cfg "something-else.cfg"

The first `cfg` will add config scope `foo` with all parameters in `something.cfg` (all these parameters will be within the `foo` hierarchy as well). Later on, `override-cfg` overrides config scope `foo` with `something-else.cfg`. More explicitly, all parameters from `something.cfg` will be removed.

#### Appending

In some cases you want to *append* to an existing scope, but while still allowing for clean overrides (with `override-cfg`) later on. In this case you can use:

    <scope>:append-cfg "<file>"

This will effectively *add* to an existing config scope (required). If a later `override-cfg` is applied on this scope, it will remove this and prior options at this config scope.

For example, in the following:

    foo:cfg "something.cfg"
    ...
    foo:append-cfg "another.cfg"
    ...
    foo:override-cfg "something-else.cfg"

As explained, `cfg` will add scope `foo` with `something.cfg`. Then, `append-cfg` inserts the parameters/config from "another.cfg" *into* that existing scope. Thus when `override-cfg` is used later, it will effectively remove all parameters from `something.cfg` *and* `another.cfg`. Only `override-cfg` is allowed to do this.

#### Removing

To remove the scope *completely*, you can use:

    <scope>:override-cfg null

Which will remove all parameters defined in the scope; however, parameters added outside of scoped config files will remain (as with `override-cfg` in general).

## Regex

Regex allows for many parameters to be set or for a config to be applied to _multiple_ scopes/hierarchies.

    ~<regex> <value>
    ~<regex>:cfg "<file>"

Where regex syntax is the same as `std::regex_match`.

## Aliases

Aliases allow for a parameter to be assigned to some "name" (does not necessarily have to be a param) or some other parameter.

    <param>=<other param or name>

## Variable Substitution

There's very limited support for some built-in variable substitution in strings, using the `"$(<name>)"` syntax. Currently these are:

* `prefix`: adds the current prefix of the scope (in config file)

For example in the config-scope of `x.y.z`:

    some_param "$(prefix).something"

`x.y.z.some_param` is assigned `"x.y.z.something"`.

## JSON

JSON value types are supported:

    <param> "<string>"
    <param> { ... }
    <param> [ ... ]
    <param> @<file>

JSON objects are *not* part of the existing hierarchy, they are a value. However, they can be adjusted using `+` (covered in next section).

Including JSON from a file is relative to the current config directory, similar to `:cfg`.

### Addendums

JSON values can also be merged with:

    +<param> { ... }
    +<param> [ ... ]
    +<param> @<file>

Merging rules for the parameter values are:

* Arrays will be concatenated
* Objects will be recursively merged (every leaf "value" will be either overwritten or newly inserted, including arrays)
* See [JSON Merge Patch](https://json.nlohmann.me/features/merge_patch/) for more information

## Conversions

| Conversion  | Type(s)       | Parameter syntax                                |
| ----------- | ------------- | ----------------------------------------------- |
| Boolean     | `bool`        | `y\|yes\|true\|on\|1` or `n\|no\|false\|off\|0` |
| Hexadecimal | 'int' types   | `0x[a-f0-9]`                                    |
| Octal       | 'int' types   | `0[0-7]`                                        |
| Scientific  | `float` types | `[\+\-]?\d+\.\d+[eE][\+\-]?\d+`                 |
| Nan/Inf     | `float` types | `inf(inity)?\|nan`                              |
| Time        | `sc_time`     | `\d+\.\d+ ?[fpnum]?s`                           |

In general, types are parsed by the following rules:

* `int`: parsed via `std::strtol`
* `unsigned int`: parsed via `std::strtoul`
* `float`: parsed via `std::strtof`
