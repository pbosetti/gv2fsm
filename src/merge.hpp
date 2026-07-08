#pragma once
#include <string>
#include <vector>

/** @brief Source language of a generated file being merged. */
enum class SourceLang { C, Cpp };

/** @brief Why a function's body had to be recovered via tree-sitter. */
enum class RecoveryReason {
  LegacyImport,   ///< The whole file predates markers: no pair anywhere on disk.
  MissingMarkers, ///< No trace of this function's marker pair on disk.
  BrokenMarkers,  ///< Stray marker line(s) with this id found, but the pair did not scan (one line deleted or id mistyped).
};

/** @brief One function whose body was recovered via tree-sitter. */
struct RecoveredFunction {
  std::string id;        ///< Function name (the marker region id).
  RecoveryReason reason; ///< Why the markers could not be used.
};

/** @brief Outcome of merging freshly generated content with an existing file. */
struct MergeResult {
  std::string text;        ///< Merged file content.
  int kept = 0;             ///< USER CODE regions whose prior body was preserved.
  int added = 0;            ///< New USER CODE regions that received the default stub.
  int orphaned = 0;         ///< Preserved bodies with no matching region in `fresh` (appended to the attic).
  std::vector<RecoveredFunction> recovered_functions; ///< Functions recovered via tree-sitter, in file order, with the reason for each.
  bool legacy_import = false; ///< True when `existing` had no markers at all and whole-file tree-sitter recovery was used.
};

/**
 * @brief Merge freshly generated content with a previously generated file,
 * preserving user-edited code inside "USER CODE BEGIN/END <id>" marker
 * regions.
 *
 * If `existing` contains no marker regions at all (a file generated before
 * markers were introduced, or hand-edited without them), falls back to a
 * one-time best-effort recovery: each function named in `fresh`'s marker
 * regions is located in `existing` via a tree-sitter parse, and its body is
 * sliced out using the same structural boundary (a `switch` statement for C
 * state functions, the final `return` statement for C++ state functions, or
 * the whole body for transition functions) used to place markers today.
 *
 * If `existing` has markers but an individual function's pair is missing or
 * malformed (a marker line deleted or its id mistyped), the same tree-sitter
 * recovery runs for just that function; any stray surviving marker line is
 * scrubbed from the recovered body so the next merge parses cleanly.
 *
 * Every function recovered via tree-sitter — in either mode — is listed in
 * MergeResult::recovered_functions along with the reason it needed recovery.
 *
 * @param fresh Freshly generated file content (always contains markers).
 * @param existing Content of the file currently on disk.
 * @param lang Source language, selects the C or C++ tree-sitter grammar for
 *             the legacy-import fallback.
 * @return Merged content plus counters describing what happened.
 */
MergeResult merge_generated(const std::string &fresh, const std::string &existing,
                            SourceLang lang);
