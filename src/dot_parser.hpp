#pragma once
#include <string>
#include <vector>

/** @brief Node parsed from a DOT graph. */
struct DotNode {
  std::string id;    ///< Node identifier.
  std::string label; ///< Optional node label from [label="..."].
};

/** @brief Edge parsed from a DOT graph. */
struct DotEdge {
  std::string from;  ///< Source node identifier.
  std::string to;    ///< Destination node identifier.
  std::string label; ///< Optional edge label from [label="..."].
};

/** @brief Parsed DOT graph data. */
struct DotGraph {
  std::string name;           ///< Graph name.
  bool directed = false;      ///< True for digraph, false otherwise.
  std::vector<DotNode> nodes; ///< Parsed nodes.
  std::vector<DotEdge> edges; ///< Parsed edges.
};

/**
 * @brief Parse a DOT file into a DotGraph structure.
 * @param filename Path to the DOT file.
 * @param graph Output graph populated on success.
 * @param error_msg Output error message populated on failure.
 * @return true when parsing succeeds, false otherwise.
 */
bool parse_dot_file(const std::string &filename, DotGraph &graph,
                    std::string &error_msg);
