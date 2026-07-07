#pragma once
#include <string>

/** @brief Source language of a generated file being merged. */
enum class SourceLang { C, Cpp };

/** @brief Outcome of merging freshly generated content with an existing file. */
struct MergeResult {
  std::string text;        ///< Merged file content.
  int kept = 0;             ///< USER CODE regions whose prior body was preserved.
  int added = 0;            ///< New USER CODE regions that received the default stub.
  int orphaned = 0;         ///< Preserved bodies with no matching region in `fresh` (appended to the attic).
  bool legacy_import = false; ///< True when `existing` had no markers and tree-sitter recovery was used.
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
 * @param fresh Freshly generated file content (always contains markers).
 * @param existing Content of the file currently on disk.
 * @param lang Source language, selects the C or C++ tree-sitter grammar for
 *             the legacy-import fallback.
 * @return Merged content plus counters describing what happened.
 */
MergeResult merge_generated(const std::string &fresh, const std::string &existing,
                            SourceLang lang);
