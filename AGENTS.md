# Panthera repository guide

## Scope and orientation

This file applies at the repository root and to all subdirectories unless a
more-local `AGENTS.md` is added later. Panthera is an Ampere SoC performance
simulator: its primary implementation is C++23/SystemC, with Python tools and
tests. The normal build is CMake, but the top-level `Makefile` is the supported
developer interface; use its targets for configuring, building, testing,
formatting, and generating documentation whenever one exists.

Important locations:

* `src/ampere/` contains Panthera modules, simulator models, tools, Python
  package, configurations, and regressions. Each module generally owns a
  `CMakeLists.txt` and may have `test/` and `testing/` children.
* `src/{elfio,effolkronium}/` are bundled or third-party components. Avoid
  broad refactors there; preserve their local conventions and change them only
  when the task requires it.
* `infrastructure/` holds hooks, formatting checks, and developer scripts.
* `nix/`, `shell.nix`, and `clang.nix` define reproducible GCC and Clang
  development environments. On supported cluster hosts, enter/enable the Nix
  environment (normally via `direnv allow`) before configuring.
* `doc/` is the MkDocs source. Its sections are `user-guide/`, `analysis/`,
  `micros/`, `tracing/`, and `development/`. `mkdocs.yml`
  is the canonical navigation and site configuration; add an entry there when
  adding a user-facing page. `doc/index.md` is the overview and
  `doc/UserGuide.md` is the main guide.

## Build and test

Run commands from the repository root. Builds default to `release` and use
`cmake-build-<type>/`; installation goes to `install/`.

```bash
make install             # configure, build, and install the default release build
make install-debug       # configure, build, and install a debug build
make build               # build without installing
make test                # build/install and run the default test selection
make unit-test           # exclude integration, regression, benchmark, and veryslow tests
make integration         # run integration and regression tests
make list-tests          # list CTest tests for the current build directory
make test TEST=name      # build and run one C++ test by its exact CTest name
make test TESTS='regex'  # run tests matching a CTest regex (including Python/Panthera tests)
```

Use a matching build-type suffix when needed, for example `make test-debug` or
`make unit-test-asan`. Pass `CTEST_ARGS=-VV` to see a test's full command.
Prefer the smallest relevant test target while iterating; run `make unit-test`
for ordinary implementation changes and add integration coverage when behavior
crosses component boundaries. `make all-tests`, `make benchmark`,
`make asan-unit-test`, `make valgrind-unit-test`, and `make coverage` are more
expensive, targeted checks. For expected performance-neutral changes, consider
`make check-drift`; it requires the project regression environment and can be
long-running.

Do not manually edit generated build, installation, coverage, or MkDocs output
(`cmake-build-*`, `install*`, `coverage/`, `public/`, or
`compile_commands.json`). Use `make reset-cache`, `make clean`, or
`make clean-<type>` only when the affected build directory is the intended
target. The build automatically initializes submodules and installs the
pre-commit hook during CMake configuration.

## Implementation and tests

* Keep changes narrowly scoped to the owning module. When adding a source,
  executable, library, generated artifact, or test, update that directory's
  `CMakeLists.txt`; files are not discovered automatically.
* Put behavior tests in `**/test/` and name them `test_*.(cc|py)`. Put reusable
  fixtures, builders, fakes, and mocks in `**/testing/`; C++ support libraries
  should normally be header-only or end in `_testing` and use an
  `ampere::*::testing` namespace.
* Register C++ tests with `add_cpp_test`, Python tests with `add_python_test`,
  and study/cheetah tests with `add_panthera_test`. Label integration tests
  `integration` (and use appropriate timeout/optional labels) so Makefile
  selectors continue to work.
* The root CMake project uses C++23 (with GNU C++ extensions disabled) and
  C23, and exports `compile_commands.json`. Preserve CMake target
  relationships rather than adding global include paths or flags.
* Some libraries depend on registration before `main()`. Follow the
  `*_raw`/whole-archive pattern described in `doc/development/linking.md` when
  adding to such a library. Add sources to the raw target and never link
  consumers against `LIBNAME_raw` directly.

## Style and quality checks

Follow `doc/development/conventions.md` and let the configured formatters make
layout decisions.

* C++: `PascalCase` types/enums, `snake_case` functions/methods/variables, and
  `UPPER_CASE` constants/macros. Prefer `auto`, `const` member functions,
  `enum class`, references where null is invalid, named structs over tuples,
  and the standard library. Avoid `using namespace`, and use braces when an
  `if` condition or body spans multiple lines.
* Python: write idiomatic code, prefer type annotations and comprehensions
  where clear, use underscore-prefixed unused variables, and follow Ruff's
  120-column, four-space configuration in `pyproject.toml`.
* Markdown, configuration, and source text must end in a newline; avoid tabs
  in the checked text formats.

Check changes with the Makefile:

```bash
make check-format         # text/newline, clang-format, and Ruff format checks
make python-lint          # Ruff lint for tracked Panthera Python files
make doc                  # build the MkDocs site after documentation changes
```

`make format`, `make clang-format`, and `make python-format` intentionally
require a clean worktree and rewrite all matching files. Do not use them over
someone else's uncommitted work. For an already-dirty tree, format only the
files you own with the configured formatter, or use the `*-changed` Makefile
targets after verifying their diff scope. Run `make check-format` before
hand-off; if unrelated pre-existing changes make a repository-wide check fail,
report that distinction clearly.

## Documentation and change hygiene

Keep documentation close to the audience it serves and use existing Markdown
heading, link, fenced-code, and relative-link conventions. Update `mkdocs.yml`
when adding a new page that should appear in the published navigation, then
validate with `make doc`. Update documentation together with any changed
command, test workflow, configuration, or user-visible behavior.

Before making changes, review `git status` and preserve unrelated work in a
dirty worktree. Do not use `git reset`, broad cleanup, or destructive
regeneration to discard or overwrite existing work. Before handing off, review
`git diff` and `git status`, keep generated and unrelated files out of the
change, and state the exact build/test/format/doc commands run (or why one was
not run). When committing, make each commit small, standalone, and atomic:
stage only one coherent change and ensure every commit builds and passes its
relevant checks so history is easy to review and can be automatically
git-bisected. Write commit messages that explain the rationale as well as the
material implementation details, so a reviewer or future code archaeologist
can understand both why and how the change was made. Wrap commit-message body
lines at 72 columns. For a performance-impacting
change, record expected drift in the review using the repository's
`Expected Perf Drift: <value or negligible>` convention.


### Labeling

Any code modifications made by AI agents should be labeled as such, ideally in
a git commit message *and* in the code. Labeling via git messages is
*absolutely* required.

For git messages, use the following commit template:

    <SUMMARY OF CHANGE, max of 50 characters>
    <DETAILS OF CHANGE, wrap at 72 characters, markdown-like bulleted list>

    Assisted-by: <AGENT NAME>:<MODEL VERSION>

For source comments (code-only), if a file/class is added with AI, there should
be a comment indicating so, in the following format:

    Assisted and/or generated by <AGENT NAME>:<MODEL VERSION>

Comments can be grouped semantically by block/function/class/file, etc.
Per-line labeling is *not* required. Only add a comment if a large amount of
code is generated from scratch.

 `AGENT NAME` may be the name of harness used. `MODEL VERSION` is the most
 specific model identifier exposed by the runtime (including reasoning level
 and harness). If only a partial identifier is available, use that partial
 identifier as-is. Do not guess missing version details. An example of the full
 name may be: "Codex:gpt-5.5 high".
