# Linking

Many of our libraries execute code before `main()` runs. The widely
used `poly` modules have a registry that executes before `main()`
runs; This affects most `ampere_*` libraries. Similarly, the generated
instruction decoders register their decoder functions before `main()`
runs. Since the linker does not see anything using the static
variables that capture the results of those functions, the function
calls are generally deleted unless we take special steps to avoid that
when linking.

The special linking steps are called "whole-archive" wrapping.  This
wrapping tells the linker to not drop unreferenced symbols. Starting with
cmake version >= 3.24, there is a mechanism for specifying this
whole-archive wrapping.

Historical note: Before cmake version 3.24, we would use
`-Wl,--whole-archive LIBNAME -Wl,--no-whole-archive` for whatever
LIBNAME we are using/referencing. `LIBNAME` would be `archx`,
`ampere_core` etc. Often we neglected to wrap library references
properly and sometimes we wrapped libraries unnecessarily. The
new cmake functionality we're using helps ensure that we do.

If you create a new library that uses code that runs before `main()`
or uses `__attribute__((constructor))` semantics, then you'll need to
copy the `archx/CMakeLists.txt` pattern used for `archx_raw` and
`archx`.

If you add a new file to a library that uses the new wrapping
mechanism (most do), add it to the `LIBNAME_raw` version along with
the other files.

Do not reference `LIBNAME_raw` when linking to other things.

