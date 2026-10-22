# Contributing

We appreciate any and all contributions! Panthera is a **reference implementation** of Ampere's software performance model, released primarily as a resource for the academic microarchitecture community. It is not under active feature development from Ampere's side, but we are committed to being good open-source citizens.

The maintainers review issues and pull requests **at least once per week**, and more frequently during periods of high activity.

## Scope

Contributions should:

* Fix bugs in existing functionality — correctness issues, build problems, and portability fixes are all welcome.
* Improve documentation — clarifications, usage examples, and corrections are especially valuable for academic users.
* Improve build system integration — making Panthera easier to set up in common research environments.

Contributions that are **out of scope** for this project:

* New microarchitectural features or modeling capabilities — we cannot validate changes to the model's methodology.
* Changes that would affect reproducibility of published results built on Panthera.
* New dependencies without prior discussion.
* Improvements to the [catscan](https://github.com/AmpereComputing/catscan) event-stream visualization tool or any of the instruction-, event-, branch-, or memory-streams or their tools contained in [perf-streams](https://github.com/AmpereComputing/perf-streams/) should be made to those repositories.

For any larger change, please **open an issue first** to discuss it with the maintainers before investing time in an implementation.

## Pull Requests

* Keep PRs focused — one fix or improvement per PR is ideal.
* Describe what problem you're solving and how you verified the fix.
* Reference any related issues (e.g., `Fixes #42`).

## License

By submitting a contribution, you agree that your work will be licensed under the [3-Clause BSD License](LICENSE) that covers this project.
