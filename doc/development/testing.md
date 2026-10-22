# Testing

## Organization

Tests are put into `**/test/*` directories and conform to `test_*.(cc|py)` for
testing a particular file/behavior.

Reusable testing utilities, such as:
  * Mocks/fakes
  * Builders
  * Fixtures

Should be in `**/testing/*` directories. For C++, sources should ideally be
header-only but if `*.cc` files are needed, you can have libraries should
conform to `*_testing`. Ideally these utility functions/classes should also be inside of an `ampere::*::testing` namespace.

## Unit Tests

### C++

We use the [googletest](https://google.github.io/googletest/) framework for C++
unit-tests. Some good resources are listed there, including the "Primer".

There's a lot of reusable utilities/mocks/etc. within `**/testing/*` directories. Some of more important ones are:

* `TestModule` (from `sc/testing/test_module`): provides an easy test harness/fixture for module-based tests
* `MockPutter/Taker/etc.` (from `sc/testing/test_interfaces.h`): provides mocks for binding with In/Out and creating expectations.

### Python

We use the [unittest](https://docs.python.org/3/library/unittest.html) framework for Python unit-tests.

## Integration Tests

Integration tests can be added in the same manner as unit-tests or use
`cheetah` (see Panthera tests below). However they should probably added with
`LABELS integration`.

## Registering Tests (i.e. CMake/CTest)

### C++

You can use the `add_cpp_test(...)` function in a `CMakeLists.txt` to add/register a C++ unit-test. For example:

```cmake
add_cpp_test(my_test SOURCES test_my_module.cc)
```

For any special linking/compile-options, you can call these functions on the
target afterwards, for example:

```cmake
add_cpp_test(my_test SOURCES test_my_module.cc)
target_link_libraries(my_test some_library)
```

Other options include:
* `OPTIONAL`: do not run by default
* `SYSTEMC`: simulation-based test, use `sc_main` test harness instead of `gtest_main`
* `TIMEOUT`: test timeout
* `SOURCES`: test sources
* `LABELS`: test labels

### Python

You can use the `add_python_test(...)` function in a `CMakeLists.txt` to add/register a python unit-test. For example:

```cmake
add_python_test(my_python_test TESTS ${CMAKE_CURRENT_SOURCE_DIR}/test_something.py)
```

If you have all tests in some directory, you can use `DISCOVER` to add all of them, for example:

```cmake
add_python_test(my_python_test DISCOVER ${CMAKE_CURRENT_SOURCE_DIR}/test)
```

Other options include:
* `DISCOVER`: run all tests within specified directory
* `SPLIT`: split into separate tests for parallelism
* `TIMEOUT`: test timeout
* `PATH`: additional python path
* `WORKING_DIRECTORY`: working directory tests are run in
* `TESTS`: python test files
* `LABELS`: test labels

### Panthera (i.e. study-like)

You can use the `add_panthera_test(...)` function in a `CMakeLists.txt` to add/register a cheetah test. For example:

```cmake
add_panthera_test(my_cheetah_test
    SPECIFICATION something.json
    WORKLOADS workloads.json)
```

Other options include:
* `KEEP`: keep results/outputs
* `OPTIONAL`: do not run by default
* `TIMEOUT`: test timeout
* `SPECIFICATION`: test specification/study file path
* `WORKLOADS`: workloads file path
* `LABELS`: test labels
