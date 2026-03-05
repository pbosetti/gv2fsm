#pragma once
#include "dot_parser.hpp"
#include <map>
#include <string>
#include <vector>

/** @brief State node in the FSM graph. */
struct State {
  std::string id;       ///< State identifier.
  std::string function; ///< State function name (e.g. "do_init" or "prefix_do_init").
};

/** @brief Directed transition between states. */
struct Transition {
  std::string from;     ///< Source state identifier.
  std::string to;       ///< Destination state identifier.
  std::string function; ///< Transition function name, may be empty (NULL).
};

/** @brief Source and sink classification of FSM states. */
struct Topology {
  std::vector<std::string> sources; ///< States with no incoming edges.
  std::vector<std::string> sinks;   ///< States with no outgoing edges.
};

/** @brief Transition path endpoint pair. */
struct TransitionPath {
  std::string from; ///< Source state identifier.
  std::string to;   ///< Destination state identifier.
};

class FSM {
public:
  /**
   * @brief Parse a DOT file and populate the FSM model.
   * @param filename Path to the DOT input file.
   * @param error_msg Output error message populated on failure.
   * @return true on success, false on parse/validation failure.
   */
  bool parse(const std::string &filename, std::string &error_msg);

  /** @brief Get the list of state identifiers. */
  std::vector<std::string> states_list() const;
  /** @brief Get the list of state function names. */
  std::vector<std::string> state_functions_list() const;
  /** @brief Get the deduplicated list of transition function names. */
  std::vector<std::string> transition_functions_list() const;

  /**
   * @brief Build transition-function matrix indexed by state order.
   * @return Matrix where entry [from_idx][to_idx] is function name or "NULL".
   */
  std::vector<std::vector<std::string>> transitions_map() const;

  /**
   * @brief Build state destinations map.
   * @return Map from state id to list of reachable destination state ids.
   */
  std::map<std::string, std::vector<std::string>> destinations() const;

  /**
   * @brief Build reverse map of transition function usage.
   * @return Map from function name to list of source/destination pairs.
   */
  std::map<std::string, std::vector<TransitionPath>> transitions_paths() const;

  /**
   * @brief Compute topology classification from transition adjacency.
   * @return Lists of source and sink states.
   */
  Topology topology() const;

  std::string project_name; ///< Project name used for generated artifacts.
  std::string description;  ///< Human-readable FSM description.
  std::string cname;        ///< Output base name (without extension).
  std::string prefix;       ///< Prefix for generated names (includes trailing '_').
  std::string dotfile;      ///< Source DOT file path.
  std::string sigint;       ///< SIGINT target state, empty when disabled.
  bool syslog = false;      ///< Enable syslog support in generated code.
  bool ino = false;         ///< Enable ino support in generated code.
  bool plain_c = true;      ///< Generate plain C API when true.

  std::vector<State> states;         ///< Parsed FSM states.
  std::vector<Transition> transitions; ///< Parsed FSM transitions.

private:
  // Adjacency matrix: _matrix[from][to] = count of edges
  std::vector<std::vector<int>> _matrix;
  std::map<std::string, int> _nodemap;
};
