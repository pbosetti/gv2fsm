# Changelog

All notable changes to this project are documented in this file. The format
is loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.0.0] - 2026-07-07

### Added

- **Non-destructive regeneration.** Generated C and C++ sources now wrap every
  state/transition function body (plus file-level `includes`/`globals` spots)
  in `/* USER CODE BEGIN <id> */ ... /* USER CODE END <id> */` markers.
- New `-u, --update` CLI flag: regenerates a source file from a changed
  `.dot` graph while copying the content of every marker region from the file
  already on disk into the new one. New states/transitions get the usual
  stub; states/transitions removed from the graph have their old body
  appended under an `/* ===== ORPHANED USER CODE ===== */` section at the end
  of the file instead of being discarded. The previous file is always copied
  to `<file>.bak` first.
- One-time legacy-import fallback for files generated before markers existed:
  `--update` on a marker-less file parses it with
  [tree-sitter](https://tree-sitter.github.io/tree-sitter/) and recovers each
  function's hand-written body from around the generated boilerplate.
  New build dependency: `tree-sitter` core plus the `tree-sitter-c` and
  `tree-sitter-cpp` grammars (fetched via CMake `FetchContent`, built from
  their committed, pre-generated parser sources — no external code-gen tool
  required at build time).
- `LICENSE` — the project is now explicitly licensed under Apache License 2.0.
- This changelog.
- A real rendered example diagram, `examples/sm.png`, generated from
  `examples/sm.dot` with Graphviz.

### Fixed

- `--prefix` no longer breaks C++ compilation: the `FiniteStateMachine` class
  now consistently uses the prefixed type/constant names throughout
  `operator()`, `setup()`, and `eval_state()` (previously several spots used
  the bare, unprefixed names).
- Custom node `[label=...]` values are honored again in C++ state dispatch;
  it previously always called `<prefix>do_<node-id>`, ignoring the label and
  failing to compile whenever a node's label differed from its id.
- `--ino` now actually emits the Arduino-flavored `.h`/`.cpp` pair described
  in the docs. It was being routed through the C++ `_impl.hpp` template,
  which has none of the Arduino-specific (`Serial`, no-SIGINT) branches.
- C++ output for graphs with no sink state no longer emits an empty
  `STATE_` token in the `run()` loop's exit condition.
- `state_names` and the SIGINT pending-transition flag in the generated C++
  header are now `inline`, fixing a multiple-definition link error when the
  header is included from more than one translation unit.
- The `-i, --ino` CLI help text described a single `.ino` file; it now
  matches the actual `.h`/`.cpp` output.

### Changed

- README rewritten: a complete CLI reference table (several existing options
  — `-p`, `-d`, `-o`, `-e`, `-s`, `-x`, `-l`, `-f` — were previously
  undocumented), corrected Arduino/example sections, and links to the
  license and this changelog.
- `CPack`'s packaged license resource now points at `LICENSE` instead of
  `README.md`.

### Testing

- 8 new Catch2 cases (44 total): C/C++ output-parity regression tests for
  `--prefix`, custom labels, no-sink graphs, `--ino`, and inclusion from two
  translation units; plus three `merge_generated` tests covering
  marker-based updates in both languages and the tree-sitter legacy-import
  path.

[2.0.0]: https://github.com/pbosetti/gv2fsm/compare/v1.3.3...v2.0.0
