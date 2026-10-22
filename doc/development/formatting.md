# Formatting

Code formatting is enforced within panthera, which means that all code must be
correctly formatted before it's allowed to merged.

## Git Blame

To ignore commits which have formatted the code, you can configure:

    git config set blame.ignoreRevsFile .git-blame-ignore-revs

Some source-hosting services also filter these revisions from their blame views.

## Verify Formatting

To check that the code is formatted, you can run:

```bash
make check-format
```

This will check:

* Text formatting (whitepsace and newlines)
* C++ code formatting (using `clang-format`)
* Python code formatting (using `ruff format`)

## Formatting your code

There's various ways to format your code, at the minimum you can use the
targets within the `Makefile`. But there's also various IDE support for
formatting.

### Makefile

Run:

```bash
make format
```

To format text, C++, and python. Or you can call the following individually:

```bash
make text-format    # Format text
make clang-format   # Format C++
make python-format  # Format python
```

Note, this will format *all* code. If you are preparing multiple commits,
format before creating later commits or use an interactive rebase to preserve
their intended boundaries.

### Neovim

You can use the following LSPs (from `neovim/nvim-lspconfig`):

* [clangd](https://clangd.llvm.org/): this provides `clang-format` formatting for C++ files
* [ruff](https://marketplace.visualstudio.com/items?itemName=charliermarsh.ruff): this provides `ruff format` formatting for Python files

When both of these are enabled, you can format a region with `gq` or use
`vim.lsp.buf.format` to format a whole buffer.

### Vim

* C++: [clang-format vim integration](https://clang.llvm.org/docs/ClangFormat.html#emacs-integration)

### Emacs

See the following resources for some options for formatting within emacs:

* C++: [clang-format emacs integration](https://clang.llvm.org/docs/ClangFormat.html#emacs-integration)
* Python: [emacs-ruff-format](https://github.com/JoshHayes/emacs-ruff-format)

Or you can use LSPs similar to neovim.

## Languages

### C++ (clang-format)

We use `clang-format` for formatting C++ code, with the config file being at:
`.clang-format`. You can see [clang-format
options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html) for more
information about the various options and what they mean.

The formatting can also be disabled for regions using comments, like:
```c++
int formatted_code;
// clang-format off
    void    unformatted_code  ;
// clang-format on
void formatted_code_again;
```

### Python (ruff)

We use `ruff format` (which is mostly compliant with `black`) for formatting
Python code, with the config in the `pyproject.toml` under the `tool.ruff` section.

The formatting can also be disabled for regions using comments, like:
```python
# fmt: off
not_formatted=3
also_not_formatted=4
# fmt: on
```
Note, sometimes the usage is not completely intuitive, where it does not
necessarily work within lines in some blocks (like a function call). In
these cases you need to move the off/on outside the block, or use
`fmt: skip` like mentioned below.

Or you can use `fmt skip` for skipping formatting on a preceding statement:
```python
a = [1, 2, 3, 4, 5] # fmt: skip
```
Note, this will apply to the statement/block. For example, if a function
call is spread across multiple lines, putting `fmt: skip` at the end will
skip formatting the whole thing.
