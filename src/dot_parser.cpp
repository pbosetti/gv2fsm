#include "dot_parser.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

// Minimal DOT parser that handles the subset used by gv2fsm:
// - digraph "name" { ... }
// - node declarations: id or id [label="..."]
// - edge declarations: id -> id or id -> id [label="..."]
// - C and C++ style comments
// - quoted strings for identifiers and labels

namespace {

class Lexer {
public:
  explicit Lexer(const std::string &input) : _input(input) {}

  enum TokenType {
    TOK_EOF,
    TOK_ID,
    TOK_STRING,
    TOK_ARROW,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_EQUALS,
    TOK_SEMI,
    TOK_COMMA,
  };

  struct Token {
    TokenType type;
    std::string value;
  };

  Token next() {
    skip_ws_and_comments();
    if (_pos >= _input.size())
      return {TOK_EOF, ""};

    char c = _input[_pos];

    if (c == '{') {
      _pos++;
      return {TOK_LBRACE, "{"};
    }
    if (c == '}') {
      _pos++;
      return {TOK_RBRACE, "}"};
    }
    if (c == '[') {
      _pos++;
      return {TOK_LBRACKET, "["};
    }
    if (c == ']') {
      _pos++;
      return {TOK_RBRACKET, "]"};
    }
    if (c == '=') {
      _pos++;
      return {TOK_EQUALS, "="};
    }
    if (c == ';') {
      _pos++;
      return {TOK_SEMI, ";"};
    }
    if (c == ',') {
      _pos++;
      return {TOK_COMMA, ","};
    }
    if (c == '-' && _pos + 1 < _input.size() && _input[_pos + 1] == '>') {
      _pos += 2;
      return {TOK_ARROW, "->"};
    }
    if (c == '"') {
      return read_string();
    }
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      return read_id();
    }

    // Skip unknown character
    _pos++;
    return next();
  }

  Token peek() {
    auto saved = _pos;
    auto tok = next();
    _pos = saved;
    return tok;
  }

private:
  void skip_ws_and_comments() {
    while (_pos < _input.size()) {
      // Skip whitespace
      if (std::isspace(static_cast<unsigned char>(_input[_pos]))) {
        _pos++;
        continue;
      }
      // C++ style comment
      if (_pos + 1 < _input.size() && _input[_pos] == '/' &&
          _input[_pos + 1] == '/') {
        while (_pos < _input.size() && _input[_pos] != '\n')
          _pos++;
        continue;
      }
      // C style comment
      if (_pos + 1 < _input.size() && _input[_pos] == '/' &&
          _input[_pos + 1] == '*') {
        _pos += 2;
        while (_pos + 1 < _input.size() &&
               !(_input[_pos] == '*' && _input[_pos + 1] == '/'))
          _pos++;
        if (_pos + 1 < _input.size())
          _pos += 2;
        continue;
      }
      // # style comment
      if (_input[_pos] == '#') {
        while (_pos < _input.size() && _input[_pos] != '\n')
          _pos++;
        continue;
      }
      break;
    }
  }

  Token read_string() {
    _pos++; // skip opening quote
    std::string result;
    while (_pos < _input.size() && _input[_pos] != '"') {
      if (_input[_pos] == '\\' && _pos + 1 < _input.size()) {
        _pos++;
        result += _input[_pos];
      } else {
        result += _input[_pos];
      }
      _pos++;
    }
    if (_pos < _input.size())
      _pos++; // skip closing quote
    return {TOK_STRING, result};
  }

  Token read_id() {
    size_t start = _pos;
    while (_pos < _input.size() &&
           (std::isalnum(static_cast<unsigned char>(_input[_pos])) ||
            _input[_pos] == '_'))
      _pos++;
    return {TOK_ID, _input.substr(start, _pos - start)};
  }

  std::string _input;
  size_t _pos = 0;
};

using TT = Lexer::TokenType;

// Parse attribute list: [ key = value, ... ]
// Returns a map of key -> value
std::map<std::string, std::string> parse_attrs(Lexer &lex) {
  std::map<std::string, std::string> attrs;
  auto tok = lex.next(); // consume '['
  while (true) {
    tok = lex.next();
    if (tok.type == TT::TOK_RBRACKET || tok.type == TT::TOK_EOF)
      break;
    if (tok.type == TT::TOK_ID || tok.type == TT::TOK_STRING) {
      std::string key = tok.value;
      auto eq = lex.next();
      if (eq.type == TT::TOK_EQUALS) {
        auto val = lex.next();
        attrs[key] = val.value;
      }
    }
    // skip commas and semicolons
    auto p = lex.peek();
    if (p.type == TT::TOK_COMMA || p.type == TT::TOK_SEMI)
      lex.next();
  }
  return attrs;
}

} // namespace

bool parse_dot_file(const std::string &filename, DotGraph &graph,
                    std::string &error_msg) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    error_msg = "Cannot open file: " + filename;
    return false;
  }

  std::stringstream ss;
  ss << file.rdbuf();
  std::string content = ss.str();

  Lexer lex(content);

  // Expect: digraph or graph
  auto tok = lex.next();
  if (tok.type != TT::TOK_ID) {
    error_msg = "Expected 'digraph' or 'graph'";
    return false;
  }

  if (tok.value == "digraph") {
    graph.directed = true;
  } else if (tok.value == "graph") {
    graph.directed = false;
    error_msg = "Graph is not directed";
    return false;
  } else {
    // Try strict digraph
    if (tok.value == "strict") {
      tok = lex.next();
      if (tok.value == "digraph") {
        graph.directed = true;
      } else {
        error_msg = "Expected 'digraph' after 'strict'";
        return false;
      }
    } else {
      error_msg = "Expected 'digraph' or 'graph', got: " + tok.value;
      return false;
    }
  }

  // Optional graph name
  tok = lex.next();
  if (tok.type == TT::TOK_ID || tok.type == TT::TOK_STRING) {
    graph.name = tok.value;
    tok = lex.next(); // should be '{'
  }
  if (tok.type != TT::TOK_LBRACE) {
    error_msg = "Expected '{' after graph declaration";
    return false;
  }

  // Set of node IDs we've seen explicitly declared or referenced in edges
  std::vector<std::string> node_order;
  auto ensure_node = [&](const std::string &id) {
    if (std::find(node_order.begin(), node_order.end(), id) ==
        node_order.end()) {
      node_order.push_back(id);
      graph.nodes.push_back({id, ""});
    }
  };

  // Parse body
  while (true) {
    tok = lex.next();
    if (tok.type == TT::TOK_RBRACE || tok.type == TT::TOK_EOF)
      break;
    if (tok.type == TT::TOK_SEMI)
      continue;

    // Skip graph-level attributes like: rankdir=LR
    if ((tok.type == TT::TOK_ID || tok.type == TT::TOK_STRING) &&
        (tok.value == "graph" || tok.value == "node" ||
         tok.value == "edge")) {
      auto p = lex.peek();
      if (p.type == TT::TOK_LBRACKET) {
        parse_attrs(lex);
        continue;
      }
    }

    if (tok.type == TT::TOK_ID || tok.type == TT::TOK_STRING) {
      std::string first_id = tok.value;

      // Check if this is: subgraph ... (skip it)
      if (first_id == "subgraph") {
        error_msg = "Subgraphs are not supported";
        return false;
      }

      auto p = lex.peek();

      if (p.type == TT::TOK_ARROW) {
        // Edge: first_id -> second_id [attrs]
        lex.next(); // consume '->'
        auto second = lex.next();
        if (second.type != TT::TOK_ID && second.type != TT::TOK_STRING) {
          error_msg = "Expected node ID after '->'";
          return false;
        }
        std::string second_id = second.value;

        ensure_node(first_id);
        ensure_node(second_id);

        DotEdge edge{first_id, second_id, ""};

        p = lex.peek();
        if (p.type == TT::TOK_LBRACKET) {
          auto attrs = parse_attrs(lex);
          if (attrs.count("label"))
            edge.label = attrs["label"];
        }

        graph.edges.push_back(edge);
      } else if (p.type == TT::TOK_LBRACKET) {
        // Node with attributes
        ensure_node(first_id);
        auto attrs = parse_attrs(lex);
        // Update the node's label
        for (auto &n : graph.nodes) {
          if (n.id == first_id) {
            if (attrs.count("label"))
              n.label = attrs["label"];
            break;
          }
        }
      } else if (p.type == TT::TOK_EQUALS) {
        // Graph attribute: key = value (skip)
        lex.next(); // consume '='
        lex.next(); // consume value
      } else {
        // Bare node declaration
        ensure_node(first_id);
      }
    }
  }

  if (graph.nodes.empty()) {
    error_msg = "Graph is empty";
    return false;
  }

  return true;
}
