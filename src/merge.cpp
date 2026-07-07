#include "merge.hpp"
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include <tree_sitter/api.h>

extern "C" {
const TSLanguage *tree_sitter_c(void);
const TSLanguage *tree_sitter_cpp(void);
}

namespace {

// One "USER CODE BEGIN <id> ... USER CODE END <id>" region found in a file.
struct MarkerRegion {
  std::string id;
  size_t region_start;  // start of the BEGIN comment line
  size_t content_start; // just after the BEGIN line's newline
  size_t content_end;   // start of the END comment line
  size_t region_end;    // just after the END line's newline
};

std::string extract_identifier(const std::string &text, size_t from) {
  size_t i = from;
  while (i < text.size() &&
         (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
    i++;
  return text.substr(from, i - from);
}

std::vector<MarkerRegion> find_marker_regions(const std::string &text) {
  static const std::string kBegin = "USER CODE BEGIN ";
  static const std::string kEndPrefix = "USER CODE END ";

  std::vector<MarkerRegion> regions;
  size_t pos = 0;
  while (true) {
    size_t b = text.find(kBegin, pos);
    if (b == std::string::npos)
      break;

    size_t line_start = text.rfind('\n', b);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;

    std::string id = extract_identifier(text, b + kBegin.size());

    size_t begin_line_end = text.find('\n', b);
    begin_line_end =
        (begin_line_end == std::string::npos) ? text.size() : begin_line_end + 1;

    std::string end_marker = kEndPrefix + id;
    size_t e = text.find(end_marker, begin_line_end);
    if (e == std::string::npos || id.empty()) {
      // Malformed / unpaired marker: skip past this BEGIN and keep scanning.
      pos = begin_line_end;
      continue;
    }

    size_t end_line_start = text.rfind('\n', e);
    end_line_start = (end_line_start == std::string::npos) ? 0 : end_line_start + 1;
    size_t end_line_end = text.find('\n', e);
    end_line_end = (end_line_end == std::string::npos) ? text.size() : end_line_end + 1;

    regions.push_back(
        {id, line_start, begin_line_end, end_line_start, end_line_end});
    pos = end_line_end;
  }
  return regions;
}

// Drops entirely-blank leading/trailing lines, but preserves the exact
// indentation of the first and last non-blank lines (which is meaningful
// user code formatting, not template margin).
std::string trim_blank_edges(const std::string &s) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos <= s.size()) {
    size_t nl = s.find('\n', pos);
    if (nl == std::string::npos) {
      lines.push_back(s.substr(pos));
      break;
    }
    lines.push_back(s.substr(pos, nl - pos));
    pos = nl + 1;
  }

  auto is_blank = [](const std::string &l) {
    return l.find_first_not_of(" \t\r") == std::string::npos;
  };

  size_t b = 0, e = lines.size();
  while (b < e && is_blank(lines[b]))
    b++;
  while (e > b && is_blank(lines[e - 1]))
    e--;
  if (b >= e)
    return "";

  std::string out;
  for (size_t i = b; i < e; i++) {
    out += lines[i];
    if (i + 1 < e)
      out += "\n";
  }
  return out;
}

std::string node_text(TSNode n, const std::string &src) {
  uint32_t s = ts_node_start_byte(n);
  uint32_t e = ts_node_end_byte(n);
  return src.substr(s, e - s);
}

// Walk down through nested declarators (pointer_declarator, function_declarator,
// ...) via the "declarator" field until an identifier is reached.
std::string declarator_name(TSNode node, const std::string &src) {
  TSNode cur = node;
  while (!ts_node_is_null(cur)) {
    std::string type = ts_node_type(cur);
    if (type == "identifier" || type == "field_identifier")
      return node_text(cur, src);
    TSNode next = ts_node_child_by_field_name(cur, "declarator", 10);
    if (ts_node_is_null(next))
      break;
    cur = next;
  }
  return {};
}

// Find a top-level (possibly nested in namespaces/templates) function
// definition named `fn_name` and return its compound_statement body node.
// Returns a null TSNode (ts_node_is_null) when not found.
TSNode find_function_body(TSNode root, const std::string &fn_name,
                          const std::string &src) {
  TSNode result{};
  bool found = false;
  std::function<void(TSNode)> visit = [&](TSNode node) {
    if (found)
      return;
    if (std::string(ts_node_type(node)) == "function_definition") {
      TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
      TSNode body = ts_node_child_by_field_name(node, "body", 4);
      if (!ts_node_is_null(declarator) && !ts_node_is_null(body) &&
          declarator_name(declarator, src) == fn_name) {
        result = body;
        found = true;
        return;
      }
    }
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
      visit(ts_node_child(node, i));
  };
  visit(root);
  return result;
}

// Locate the structural boundary that today's templates place the closing
// USER CODE marker at: the start of a direct-child switch_statement (C state
// functions), else the start of the LAST direct-child return_statement (C++
// state functions), else the end of the body (transition functions, which
// have no generated epilogue at all).
uint32_t find_boundary(TSNode body) {
  uint32_t start = ts_node_start_byte(body) + 1; // just after '{'
  uint32_t end = ts_node_end_byte(body) - 1;      // just before '}'
  uint32_t boundary = end;

  bool found_switch = false;
  uint32_t n = ts_node_named_child_count(body);
  for (uint32_t i = 0; i < n; i++) {
    TSNode child = ts_node_named_child(body, i);
    if (std::string(ts_node_type(child)) == "switch_statement") {
      boundary = ts_node_start_byte(child);
      found_switch = true;
      break;
    }
  }
  if (!found_switch) {
    for (uint32_t i = 0; i < n; i++) {
      TSNode child = ts_node_named_child(body, i);
      if (std::string(ts_node_type(child)) == "return_statement")
        boundary = ts_node_start_byte(child);
    }
  }
  if (boundary < start)
    boundary = end;
  return boundary;
}

struct TSFile {
  TSParser *parser = nullptr;
  TSTree *tree = nullptr;
  TSNode root{};

  TSFile(const std::string &src, SourceLang lang) {
    parser = ts_parser_new();
    ts_parser_set_language(parser, lang == SourceLang::C ? tree_sitter_c()
                                                         : tree_sitter_cpp());
    tree = ts_parser_parse_string(parser, nullptr, src.c_str(),
                                  static_cast<uint32_t>(src.size()));
    root = ts_tree_root_node(tree);
  }
  TSFile(const TSFile &) = delete;
  TSFile &operator=(const TSFile &) = delete;
  ~TSFile() {
    ts_tree_delete(tree);
    ts_parser_delete(parser);
  }
};

} // namespace

MergeResult merge_generated(const std::string &fresh, const std::string &existing,
                            SourceLang lang) {
  MergeResult result;
  auto fresh_regions = find_marker_regions(fresh);
  auto existing_regions = find_marker_regions(existing);

  std::map<std::string, std::string> preserved;

  if (!existing_regions.empty()) {
    for (auto &r : existing_regions)
      preserved[r.id] = existing.substr(r.content_start, r.content_end - r.content_start);
  } else if (!existing.empty()) {
    result.legacy_import = true;

    TSFile fresh_ts(fresh, lang);
    TSFile existing_ts(existing, lang);

    for (auto &r : fresh_regions) {
      if (r.id == "includes" || r.id == "globals")
        continue;

      TSNode existing_body = find_function_body(existing_ts.root, r.id, existing);
      if (ts_node_is_null(existing_body))
        continue;

      uint32_t ex_start = ts_node_start_byte(existing_body) + 1;
      uint32_t boundary = find_boundary(existing_body);
      std::string raw = existing.substr(ex_start, boundary - ex_start);

      // The generated prologue (e.g. the `next_state = ...UNIMPLEMENTED;`
      // declaration, or a signal()/syslog() call) sits before the marker in
      // `fresh` too; if `existing`'s body starts with that same text, it is
      // boilerplate the user never touched, not part of their code.
      TSNode fresh_body = find_function_body(fresh_ts.root, r.id, fresh);
      if (!ts_node_is_null(fresh_body)) {
        uint32_t fr_start = ts_node_start_byte(fresh_body) + 1;
        if (r.region_start >= fr_start) {
          std::string fresh_prologue =
              fresh.substr(fr_start, r.region_start - fr_start);
          if (raw.compare(0, fresh_prologue.size(), fresh_prologue) == 0)
            raw = raw.substr(fresh_prologue.size());
        }
      }

      preserved[r.id] = trim_blank_edges(raw);
    }
  }

  std::string out;
  std::set<std::string> consumed;
  size_t cursor = 0;
  for (auto &r : fresh_regions) {
    out += fresh.substr(cursor, r.content_start - cursor);
    auto it = preserved.find(r.id);
    if (it != preserved.end()) {
      out += it->second;
      if (!it->second.empty())
        out += "\n";
      consumed.insert(r.id);
      result.kept++;
    } else {
      out += fresh.substr(r.content_start, r.content_end - r.content_start);
      result.added++;
    }
    cursor = r.content_end;
  }
  out += fresh.substr(cursor);

  std::string attic;
  for (auto &[id, body] : preserved) {
    if (consumed.count(id))
      continue;
    result.orphaned++;
    attic += "/* USER CODE ORPHANED " + id +
            " (no longer present in the .dot graph) */\n";
    attic += body;
    if (!body.empty() && body.back() != '\n')
      attic += "\n";
    attic += "\n";
  }
  if (!attic.empty()) {
    out += "\n/* ===== ORPHANED USER CODE (from previous generation) ===== */\n";
    out += attic;
    out += "/* ===== END ORPHANED USER CODE ===== */\n";
  }

  result.text = out;
  return result;
}
