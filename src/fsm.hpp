#pragma once
#include "dot_parser.hpp"
#include <map>
#include <string>
#include <vector>

struct State {
  std::string id;
  std::string function; // e.g. "do_init" or "prefix_do_init"
};

struct Transition {
  std::string from;
  std::string to;
  std::string function; // may be empty (NULL)
};

struct Topology {
  std::vector<std::string> sources; // nodes with no incoming edges
  std::vector<std::string> sinks;   // nodes with no outgoing edges
};

struct TransitionPath {
  std::string from;
  std::string to;
};

class FSM {
public:
  // Parse a DOT file and populate the FSM
  bool parse(const std::string &filename, std::string &error_msg);

  // Accessor helpers
  std::vector<std::string> states_list() const;
  std::vector<std::string> state_functions_list() const;
  std::vector<std::string> transition_functions_list() const;

  // Map: transitions_map[from_idx][to_idx] = function name or "NULL"
  std::vector<std::vector<std::string>> transitions_map() const;

  // Map: state_id -> list of destination state_ids
  std::map<std::string, std::vector<std::string>> destinations() const;

  // Map: function_name -> list of {from, to} paths
  std::map<std::string, std::vector<TransitionPath>> transitions_paths() const;

  // Compute topology (sources and sinks based on adjacency matrix)
  Topology topology() const;

  // Data
  std::string project_name;
  std::string description;
  std::string cname;     // output base name (without extension)
  std::string prefix;    // prefix for generated names (includes trailing _)
  std::string dotfile;
  std::string sigint;    // SIGINT target state, empty if none
  bool syslog = false;
  bool ino = false;
  bool plain_c = true;

  std::vector<State> states;
  std::vector<Transition> transitions;

private:
  // Adjacency matrix: _matrix[from][to] = count of edges
  std::vector<std::vector<int>> _matrix;
  std::map<std::string, int> _nodemap;
};
