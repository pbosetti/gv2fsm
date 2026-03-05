#pragma once
#include <string>
#include <vector>

struct DotNode {
  std::string id;
  std::string label; // from [label="..."]
};

struct DotEdge {
  std::string from;
  std::string to;
  std::string label; // from [label="..."]
};

struct DotGraph {
  std::string name;
  bool directed = false;
  std::vector<DotNode> nodes;
  std::vector<DotEdge> edges;
};

// Parse a DOT file. Returns true on success.
// On failure, sets error_msg and returns false.
bool parse_dot_file(const std::string &filename, DotGraph &graph,
                    std::string &error_msg);
