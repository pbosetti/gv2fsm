#include "fsm.hpp"
#include <algorithm>
#include <numeric>
#include <regex>

bool FSM::parse(const std::string &filename, std::string &error_msg) {
  DotGraph graph;
  if (!parse_dot_file(filename, graph, error_msg))
    return false;

  if (!graph.directed) {
    error_msg = "Graph is not directed";
    return false;
  }

  dotfile = filename;

  // Set description from graph name if not already set
  if (description.empty() && !graph.name.empty())
    description = graph.name;

  // Set cname from filename if not already set
  if (cname.empty()) {
    auto base = filename;
    auto slash = base.rfind('/');
    if (slash != std::string::npos)
      base = base.substr(slash + 1);
    auto dot = base.rfind('.');
    if (dot != std::string::npos)
      base = base.substr(0, dot);
    cname = base;
  }

  int n = static_cast<int>(graph.nodes.size());
  _matrix.assign(n, std::vector<int>(n, 0));

  // Build states
  int idx = 0;
  for (auto &node : graph.nodes) {
    std::string label;
    if (node.label.empty() ||
        (node.label == node.id &&
         node.label.substr(0, 3) != "do_")) {
      label = "do_" + node.id;
    } else {
      label = node.label;
    }
    _nodemap[node.id] = idx++;
    states.push_back({node.id, prefix + label});
  }

  // Build transitions
  for (auto &edge : graph.edges) {
    int fi = _nodemap[edge.from];
    int ti = _nodemap[edge.to];
    _matrix[fi][ti]++;

    std::string func;
    if (edge.label.empty()) {
      // No label = no transition function
      func = "";
    } else if (edge.label.find('#') != std::string::npos) {
      // '#' means auto-generate name from_to
      func = edge.from + "_to_" + edge.to;
    } else {
      func = edge.label;
    }

    transitions.push_back(
        {edge.from, edge.to,
         func.empty() ? "" : prefix + func});
  }

  // Validate sigint state
  if (!sigint.empty()) {
    bool found = false;
    for (auto &s : states) {
      if (s.id == sigint) {
        found = true;
        break;
      }
    }
    if (!found) {
      error_msg = "Missing SIGINT state " + sigint;
      return false;
    }
  }

  return true;
}

std::vector<std::string> FSM::states_list() const {
  std::vector<std::string> result;
  for (auto &s : states)
    result.push_back(s.id);
  return result;
}

std::vector<std::string> FSM::state_functions_list() const {
  std::vector<std::string> result;
  for (auto &s : states)
    result.push_back(s.function);
  return result;
}

std::vector<std::string> FSM::transition_functions_list() const {
  std::vector<std::string> result;
  for (auto &t : transitions) {
    if (!t.function.empty() &&
        std::find(result.begin(), result.end(), t.function) == result.end()) {
      result.push_back(t.function);
    }
  }
  return result;
}

std::vector<std::vector<std::string>> FSM::transitions_map() const {
  int n = static_cast<int>(states.size());
  auto sl = states_list();
  std::map<std::string, int> idx;
  for (int i = 0; i < n; i++)
    idx[sl[i]] = i;

  std::vector<std::vector<std::string>> map(
      n, std::vector<std::string>(n, "NULL"));

  for (auto &t : transitions) {
    map[idx.at(t.from)][idx.at(t.to)] =
        t.function.empty() ? "NULL" : t.function;
  }
  return map;
}

std::map<std::string, std::vector<std::string>> FSM::destinations() const {
  std::map<std::string, std::vector<std::string>> dest;
  for (auto &s : states)
    dest[s.id] = {};
  for (auto &t : transitions)
    dest[t.from].push_back(t.to);
  return dest;
}

std::map<std::string, std::vector<TransitionPath>>
FSM::transitions_paths() const {
  std::map<std::string, std::vector<TransitionPath>> path;
  for (auto &t : transitions) {
    if (!t.function.empty()) {
      path[t.function].push_back({t.from, t.to});
    }
  }
  return path;
}

Topology FSM::topology() const {
  Topology res;
  int n = static_cast<int>(states.size());
  auto sl = states_list();

  for (int i = 0; i < n; i++) {
    int from_sum = 0;
    int to_sum = 0;
    for (int j = 0; j < n; j++) {
      from_sum += _matrix[i][j];
      to_sum += _matrix[j][i];
    }
    if (from_sum == 0)
      res.sinks.push_back(sl[i]);
    if (to_sum == 0)
      res.sources.push_back(sl[i]);
  }
  return res;
}
