#include "generator.hpp"
#include "version.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <inja/inja.hpp>
#include <sstream>

static std::string current_time_string() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

static std::string to_upper(const std::string &s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return r;
}

static std::string basename_no_ext(const std::string &path) {
  auto s = path;
  auto slash = s.rfind('/');
  if (slash != std::string::npos)
    s = s.substr(slash + 1);
  auto dot = s.rfind('.');
  if (dot != std::string::npos)
    s = s.substr(0, dot);
  return s;
}

// Pad string to given width
static std::string ljust(const std::string &s, size_t width) {
  if (s.size() >= width)
    return s;
  return s + std::string(width - s.size(), ' ');
}

// Build the JSON data used by all templates
static nlohmann::json build_data(const FSM &fsm) {
  nlohmann::json data;

  data["project_name"] =
      fsm.project_name.empty() ? fsm.dotfile : fsm.project_name;
  data["description"] =
      fsm.description.empty() ? "<none given>" : fsm.description;
  data["version"] = GV2FSM_VERSION;
  data["generation_date"] = current_time_string();
  data["dotfile"] = fsm.dotfile;
  data["prefix"] = fsm.prefix;
  data["prefix_upper"] = to_upper(fsm.prefix);
  data["cname"] = fsm.cname;
  data["cname_upper"] = to_upper(fsm.cname);
  std::string cname_base = basename_no_ext(fsm.cname + ".x");  // strip dir prefix
  data["cname_base"] = cname_base;
  data["cname_base_upper"] = to_upper(cname_base);
  data["is_ino"] = fsm.ino;
  data["use_syslog"] = fsm.syslog;
  data["plain_c"] = fsm.plain_c;
  data["has_prefix"] = !fsm.prefix.empty();

  std::string ns = fsm.project_name.empty() ? "FSM" : fsm.project_name;
  data["namespace"] = ns;

  bool has_sigint = !fsm.sigint.empty();
  data["has_sigint"] = has_sigint;
  data["sigint"] = fsm.sigint;
  data["sigint_state_upper"] =
      has_sigint ? to_upper(fsm.prefix + "STATE_" + fsm.sigint) : "";

  // States
  nlohmann::json states_arr = nlohmann::json::array();
  for (size_t i = 0; i < fsm.states.size(); i++) {
    nlohmann::json s;
    s["id"] = fsm.states[i].id;
    s["id_upper"] = to_upper(fsm.states[i].id);
    s["function"] = fsm.states[i].function;
    s["is_first"] = (i == 0);
    states_arr.push_back(s);
  }
  data["states"] = states_arr;
  data["num_states"] = fsm.states.size();

  // States list (just names)
  auto sl = fsm.states_list();
  data["states_list"] = sl;

  // State functions list
  auto sfl = fsm.state_functions_list();
  data["state_functions_list"] = sfl;

  // Transition functions list (non-empty function names, unique)
  auto tfl = fsm.transition_functions_list();
  data["transition_functions_list"] = tfl;
  data["has_transitions"] = !tfl.empty();
  data["num_transition_functions"] = tfl.size();

  // Transitions map: [from_idx][to_idx] = function or "NULL"
  auto tm = fsm.transitions_map();
  data["transitions_map"] = tm;

  // Destinations: state_id -> [dest_ids]
  auto dest = fsm.destinations();
  data["destinations"] = dest;

  // Transition paths: function_name -> [{from, to}, ...]
  auto tp = fsm.transitions_paths();
  nlohmann::json tpaths;
  for (auto &[fn, paths] : tp) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto &p : paths) {
      arr.push_back({{"from", p.from}, {"to", p.to},
                     {"from_upper", to_upper(p.from)},
                     {"to_upper", to_upper(p.to)}});
    }
    tpaths[fn] = arr;
  }
  data["transitions_paths"] = tpaths;

  // Pre-compute transition info array for templates
  // Each entry: {name, paths: [{from, to, from_upper, to_upper}], count, plural}
  nlohmann::json transition_info = nlohmann::json::array();
  for (auto &fn : tfl) {
    nlohmann::json ti;
    ti["name"] = fn;
    ti["paths"] = tpaths.contains(fn) ? tpaths[fn] : nlohmann::json::array();
    int cnt = ti["paths"].size();
    ti["count"] = cnt;
    ti["plural"] = (cnt != 1);
    // First path for transition add_transition calls
    if (cnt > 0) {
      ti["first_from_upper"] = ti["paths"][0]["from_upper"];
      ti["first_to_upper"] = ti["paths"][0]["to_upper"];
    }
    transition_info.push_back(ti);
  }
  data["transition_info"] = transition_info;

  // Topology
  auto topo = fsm.topology();
  data["sources"] = topo.sources;
  data["sinks"] = topo.sinks;
  data["num_sinks"] = topo.sinks.size();
  data["first_source"] = topo.sources.empty() ? "" : topo.sources[0];
  data["first_sink"] = topo.sinks.empty() ? "" : topo.sinks[0];
  data["first_sink_upper"] =
      topo.sinks.empty() ? "" : to_upper(topo.sinks[0]);
  data["first_state_id_upper"] =
      fsm.states.empty() ? "" : to_upper(fsm.states[0].id);

  // Pre-compute per-state destination info used by templates
  nlohmann::json state_dest_info = nlohmann::json::array();
  auto raw_dest = fsm.destinations();
  for (auto &s : fsm.states) {
    nlohmann::json info;
    info["id"] = s.id;
    info["id_upper"] = to_upper(s.id);
    info["function"] = s.function;

    auto d = raw_dest[s.id];
    bool stable =
        std::find(d.begin(), d.end(), s.id) != d.end();
    info["stable"] = stable;

    // Build dest list as STATE_XX names
    std::vector<std::string> dest_states;
    for (auto &dn : d)
      dest_states.push_back(to_upper(fsm.prefix + "STATE_" + dn));

    if (dest_states.empty() || stable)
      dest_states.insert(dest_states.begin(),
                         to_upper(fsm.prefix) + "NO_CHANGE");

    info["dest_states"] = dest_states;
    info["default_dest"] = dest_states.front();

    bool is_source = (!topo.sources.empty() && topo.sources[0] == s.id);
    info["is_source"] = is_source;
    info["sigint_override"] = has_sigint && stable && !is_source;

    state_dest_info.push_back(info);
  }
  data["state_dest_info"] = state_dest_info;

  // Compute max widths for alignment
  size_t max_sf_len = 0;
  for (auto &sf : sfl)
    max_sf_len = std::max(max_sf_len, sf.size());
  data["max_sf_len"] = max_sf_len;

  size_t max_tf_len = 4; // "NULL"
  for (auto &tf : tfl)
    max_tf_len = std::max(max_tf_len, tf.size());
  data["max_tf_len"] = max_tf_len;

  size_t max_state_len = 7; // "states:"
  for (auto &sn : sl)
    max_state_len = std::max(max_state_len, sn.size());
  data["max_state_len"] = max_state_len;

  return data;
}

// ============================================================
// Template strings using inja syntax
// ============================================================

static const char *HEADER_TEMPLATE = R"(/******************************************************************************
Finite State Machine
Project: {{ project_name }}
Description: {{ description }}

Generated by gv2fsm, see https://github.com/pbosetti/gv2fsm
gv2fsm version {{ version }}
Generation date: {{ generation_date }}
Generated from: {{ dotfile }}
The finite state machine has:
  {{ num_states }} states
  {{ num_transition_functions }} transition functions
{% if has_prefix %}
Functions and types have been generated with prefix "{{ prefix }}"
{% endif %}
******************************************************************************/
)";

// C Header (.h)
static const char *HH_TEMPLATE = R"({% if not is_ino %}
#ifndef {{ cname_base_upper }}_H
#define {{ cname_base_upper }}_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdlib.h>
{% else %}#include <arduino.h>
{% endif %}
// State data object
// By default set to void; override this typedef or load the proper
// header if you need
typedef void {{ prefix }}state_data_t;
{% if not is_ino %}

// NOTHING SHALL BE CHANGED AFTER THIS LINE!
{% endif %}

// List of states
typedef enum {
{% for s in states %}  {{ prefix_upper }}STATE_{{ s.id_upper }}{% if s.is_first %} = 0{% endif %},  
{% endfor %}  {{ prefix_upper }}NUM_STATES,
  {{ prefix_upper }}NO_CHANGE
} {{ prefix }}state_t;

// State human-readable names
extern const char *{{ prefix }}state_names[];

{% if has_transitions %}// State function and state transition prototypes
typedef {{ prefix }}state_t state_func_t({{ prefix }}state_data_t *data);
typedef void transition_func_t({{ prefix }}state_data_t *data);
{% else %}// State function prototype
typedef {{ prefix }}state_t state_func_t({{ prefix }}state_data_t *data);
{% endif %}
// State functions
{% for s in state_dest_info %}// Function to be executed in state {{ s.id }}
// valid return states: {{ join(s.dest_states, ", ") }}
{{ prefix }}state_t {{ s.function }}({{ prefix }}state_data_t *data);
{% endfor %}

// List of state functions
extern state_func_t *const {{ prefix }}state_table[{{ prefix_upper }}NUM_STATES];


{% if has_transitions %}// Transition functions
{% for t in transition_functions_list %}void {{ t }}({{ prefix }}state_data_t *data);
{% endfor %}
// Table of transition functions
extern transition_func_t *const {{ prefix }}transition_table[{{ prefix_upper }}NUM_STATES][{{ prefix_upper }}NUM_STATES];
{% else %}// No transition functions
{% endif %}
// state manager
{{ prefix }}state_t {{ prefix }}run_state({{ prefix }}state_t cur_state, {{ prefix }}state_data_t *data);

{% if not is_ino %}#ifdef __cplusplus
}
#endif
#endif // {{ cname_base_upper }}_H
{% endif %})";

// C Source (.c)
static const char *CC_TEMPLATE =
    R"({% if not is_ino %}{% if use_syslog %}#include <syslog.h>
{% endif %}{% endif %}#include "{{ cname_base }}.h"
{% if has_sigint %}
// Install signal handler: 
// SIGINT requests a transition to state {{ sigint }}
#include <signal.h>
static int _exit_request = 0;
static void signal_handler(int signal) {
  if (signal == SIGINT) {
    _exit_request = 1;{% if use_syslog and not is_ino %}
    syslog(LOG_WARNING, "[FSM] SIGINT transition to {{ sigint }}");{% endif %}{% if use_syslog and is_ino %}
    Serial.println("[FSM] SIGINT transition to {{ sigint }}");{% endif %}
  }
}
{% endif %}
// SEARCH FOR Your Code Here FOR CODE INSERTION POINTS!

// GLOBALS
// State human-readable names
const char *{{ prefix }}state_names[] = { {% for s in states_list %}"{{ s }}"{% if not loop.is_last %}, {% endif %}{% endfor %} };

// List of state functions
state_func_t *const {{ prefix }}state_table[{{ prefix_upper }}NUM_STATES] = {
{% for s in states %}  {{ s.function }}, // in state {{ s.id }}
{% endfor %}};
{% if has_transitions %}
// Table of transition functions
transition_func_t *const {{ prefix }}transition_table[{{ prefix_upper }}NUM_STATES][{{ prefix_upper }}NUM_STATES] = {
{% for row in transitions_map %}  { {% for cell in row %}{{ cell }}{% if not loop.is_last %}, {% endif %}{% endfor %} }, 
{% endfor %}};
{% else %}// No transition functions
{% endif %}
/*  ____  _        _       
 * / ___|| |_ __ _| |_ ___ 
 * \___ \| __/ _` | __/ _ \
 *  ___) | || (_| | ||  __/
 * |____/ \__\__,_|\__\___|
 *                         
 *   __                  _   _                 
 *  / _|_   _ _ __   ___| |_(_) ___  _ __  ___ 
 * | |_| | | | '_ \ / __| __| |/ _ \| '_ \/ __|
 * |  _| |_| | | | | (__| |_| | (_) | | | \__ \
 * |_|  \__,_|_| |_|\___|\__|_|\___/|_| |_|___/
 */                                             
{% for s in state_dest_info %}
// Function to be executed in state {{ s.id }}
// valid return states: {{ join(s.dest_states, ", ") }}
{% if s.sigint_override %}// SIGINT triggers an emergency transition to {{ sigint }}
{% endif %}{{ prefix }}state_t {{ s.function }}({{ prefix }}state_data_t *data) {
  {{ prefix }}state_t next_state = {{ s.default_dest }};
{% if has_sigint and s.is_source %}  signal(SIGINT, signal_handler); 
  {% endif %}{% if use_syslog and not is_ino %}  syslog(LOG_INFO, "[FSM] In state {{ s.id }}");
{% endif %}{% if use_syslog and is_ino %}  Serial.println("[FSM] In state {{ s.id }}");
{% endif %}  /* Your Code Here */
  
  switch (next_state) {
{% for d in s.dest_states %}  case {{ d }}:
{% endfor %}    break;
  default:
{% if use_syslog and not is_ino %}    syslog(LOG_WARNING, "[FSM] Cannot pass from {{ s.id }} to %s, remaining in this state", {{ prefix }}state_names[next_state]);
{% endif %}{% if use_syslog and is_ino %}    Serial.print("[FSM] Cannot pass from {{ s.id }} to ");
    Serial.print({{ prefix }}state_names[next_state]);
    Serial.println(", remaining in this state");
{% endif %}    next_state = {{ prefix_upper }}NO_CHANGE;
  }
{% if s.sigint_override %}  // SIGINT transition override
  if (_exit_request) 
    next_state = {{ sigint_state_upper }};
{% endif %}
  return next_state;
}

{% endfor %}
{% if has_transitions %}/*  _____                    _ _   _              
 * |_   _| __ __ _ _ __  ___(_) |_(_) ___  _ __   
 *   | || '__/ _` | '_ \/ __| | __| |/ _ \| '_ \
 *   | || | | (_| | | | \__ \ | |_| | (_) | | | | 
 *   |_||_|  \__,_|_| |_|___/_|\__|_|\___/|_| |_| 
 *                                                
 *   __                  _   _                 
 *  / _|_   _ _ __   ___| |_(_) ___  _ __  ___ 
 * | |_| | | | '_ \ / __| __| |/ _ \| '_ \/ __|
 * |  _| |_| | | | | (__| |_| | (_) | | | \__ \
 * |_|  \__,_|_| |_|\___|\__|_|\___/|_| |_|___/
 */    
                                          
{% for ti in transition_info %}// This function is called in {{ ti.count }} transition{% if ti.plural %}s{% endif %}:
{% for e in ti.paths %}// {{ loop.index1 }}. from {{ e.from }} to {{ e.to }}
{% endfor %}void {{ ti.name }}({{ prefix }}state_data_t *data) {
{% if use_syslog and not is_ino %}  syslog(LOG_INFO, "[FSM] State transition {{ ti.name }}");
{% endif %}{% if use_syslog and is_ino %}  Serial.println("[FSM] State transition {{ ti.name }}");
{% endif %}  /* Your Code Here */
}

{% endfor %}{% endif %}
/*  ____  _        _        
 * / ___|| |_ __ _| |_ ___  
 * \___ \| __/ _` | __/ _ \
 *  ___) | || (_| | ||  __/ 
 * |____/ \__\__,_|\__\___| 
 *                          
 *                                              
 *  _ __ ___   __ _ _ __   __ _  __ _  ___ _ __ 
 * | '_ ` _ \ / _` | '_ \ / _` |/ _` |/ _ \ '__|
 * | | | | | | (_| | | | | (_| | (_| |  __/ |   
 * |_| |_| |_|\__,_|_| |_|\__,_|\__, |\___|_|   
 *                              |___/           
 */

{{ prefix }}state_t {{ prefix }}run_state({{ prefix }}state_t cur_state, {{ prefix }}state_data_t *data) {
  {{ prefix }}state_t new_state = {{ prefix }}state_table[cur_state](data);
  if (new_state == {{ prefix_upper }}NO_CHANGE) new_state = cur_state;
{% if has_transitions %}
  transition_func_t *transition = {{ prefix }}transition_table[cur_state][new_state];
  if (transition)
    transition(data);
{% endif %}
  return new_state;
};
{% if is_ino %}
/* Example usage:
{{ prefix }}state_data_t data = {count: 1};

void loop() {
  static {{ prefix }}state_t cur_state = {{ prefix_upper }}STATE_INIT;
  cur_state = {{ prefix }}run_state(cur_state, &data);
}
*/
{% else %}
#ifdef TEST_MAIN
#include <unistd.h>
int main() {
  {{ prefix }}state_t cur_state = {{ prefix_upper }}STATE_{{ first_state_id_upper }};
{% if use_syslog %}
  openlog("SM", LOG_PID | LOG_PERROR, LOG_USER);
  syslog(LOG_INFO, "Starting SM");
{% endif %}  do {
    cur_state = {{ prefix }}run_state(cur_state, NULL);
    sleep(1);
{% if num_sinks == 1 %}  } while (cur_state != {{ prefix_upper }}STATE_{{ first_sink_upper }});
  {{ prefix }}run_state(cur_state, NULL);
{% else %}  } while (1);
{% endif %}  return 0;
}
#endif
{% endif %})";

// C++ Header (.hpp)
static const char *HPP_TEMPLATE =
    R"(#ifndef {{ cname_base_upper }}_HPP
#define {{ cname_base_upper }}_HPP
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
{% if use_syslog %}#include <syslog.h>
{% endif %}{% if has_sigint %}// Install signal handler: 
// SIGINT requests a transition to state {{ sigint }}
#include <csignal>
{% endif %}

using namespace std::string_literals;
namespace {{ namespace }} {
{% if has_sigint %}static bool {{ sigint }}_requested = false;
{% endif %}
// List of states
typedef enum {
{% for s in states %}  {{ prefix_upper }}STATE_{{ s.id_upper }},
{% endfor %}  {{ prefix_upper }}NUM_STATES,
  {{ prefix_upper }}NO_CHANGE,
  {{ prefix_upper }}UNIMPLEMENTED
} {{ prefix }}state_t;

// State human-readable names
std::map<{{ prefix }}state_t, char const *> state_names = {
{% for s in states %}  { {{ prefix_upper }}STATE_{{ s.id_upper }}, "{{ s.id_upper }}" },
{% endfor %}  { {{ prefix_upper }}NUM_STATES, "NUM_STATES" },
  { {{ prefix_upper }}NO_CHANGE, "NO_CHANGE" },
  { {{ prefix_upper }}UNIMPLEMENTED, "UNIMPLEMENTED" }
};

// Custom state functions:
{% for s in states %}template<class T> 
{{ prefix }}state_t {{ s.function }}(T &data);
{% endfor %}
{% if has_transitions %}// Custom transition functions:
{% for t in transition_functions_list %}template<class T>
void {{ t }}(T &data);
{% endfor %}{% endif %}
// Finite State Machine class
template <typename DATA_T> 
class FiniteStateMachine {

// Function templates
using state_fun = std::function<{{ prefix }}state_t(DATA_T &data)>;
using transition_fun = std::function<void(DATA_T &data)>;
using operation_fun = std::function<void(DATA_T &data)>;

private:
  std::pair<{{ prefix }}state_t, {{ prefix }}state_t> _state{ {{ prefix_upper }}STATE_{{ states.0.id_upper }}, {{ prefix_upper }}STATE_{{ states.0.id_upper }} };
  std::map<{{ prefix }}state_t, state_fun> _states;
  std::map<{{ prefix }}state_t, std::map<{{ prefix }}state_t, transition_fun>> _transitions;
  std::function<void()> _timing_func;
  DATA_T *_data;

public:

  FiniteStateMachine(DATA_T *data) : _data(data) {
    install_functions();
  }
  ~FiniteStateMachine(){};

  void set_timing_function(std::function<void()> timing_func) {
    _timing_func = timing_func;
  }

  void add_state({{ prefix }}state_t name, state_fun func) { _states[name] = func; }

  void add_transition({{ prefix }}state_t from, {{ prefix }}state_t to, transition_fun func) {
    _transitions[from][to] = func;
  }

  inline {{ prefix }}state_t state() { return _state.second; }
  inline std::string state_name() { return std::string(state_names[_state.second]); }
  inline {{ prefix }}state_t prev_state() { return _state.first; }

  {{ prefix }}state_t operator()({{ prefix }}state_t state) {
    if (_states.find(state) == _states.end()) {
      throw std::runtime_error("State not found: "s + state_names[state]);
    }
    state_t next = _states[state](*_data);
    if (next == NO_CHANGE) {
      next = state;
    }
    return next;
  }

  void operator()({{ prefix }}state_t from, {{ prefix }}state_t to) {
    if (_transitions.find(from) != _transitions.end()) {
      if (_transitions[from].find(to) != _transitions[from].end()) {
        _transitions[from][to](*_data);
      }
    }
  }


  // Setup initial state links
  void setup(state_t state) {
    {% if has_sigint %}{{ namespace }}::{{ sigint }}_requested = false;
    {% endif %}_state.first = state;
    _state.second = state;
  }

  // Evaluate the current state and update the next state
  // to be used when main loop is customized (i.e., not using FSM::run())
  state_t eval_state() {
      _state.first = _state.second;
      _state.second = (*this)(_state.second);
      (*this)(_state.first, _state.second);
      return _state.second;
  }

  // Run the FSM from a given state
  void run({{ prefix }}state_t state, operation_fun operation = nullptr) {
    setup(state);
{% if has_sigint %}    std::signal(SIGINT, [](int signum) {
{% if use_syslog %}      syslog(LOG_WARNING, "[FSM] SIGINT transition to {{ sigint }}");
{% endif %}      {{ namespace }}::{{ sigint }}_requested = true; 
    });
{% endif %}    do {
      if (operation) {
        operation(*_data);
      }
      eval_state();
      if (_timing_func) {
        _timing_func();
      }
    } while (_state.second != {{ prefix_upper }}STATE_{{ first_sink_upper }});
    // Call the exit state once more:
    (*this)({{ prefix_upper }}STATE_{{ first_sink_upper }});
{% if has_sigint %}    std::signal(SIGINT, SIG_DFL);
{% endif %}  }

  // Run the FSM from the initial state
  void run(operation_fun operation = nullptr) { run({{ prefix_upper }}STATE_{{ states.0.id_upper }}, operation); }

  // install state and transition functions
  void install_functions() {

    // State functions
{% for s in state_dest_info %}    add_state({{ namespace }}::{{ prefix_upper }}STATE_{{ s.id_upper }}, [](DATA_T &data) -> {{ namespace }}::{{ prefix }}state_t {
{% if use_syslog %}      syslog(LOG_INFO, "[FSM] In state {{ s.id_upper }}");
{% endif %}      {{ namespace }}::{{ prefix }}state_t next_state = {{ prefix }}do_{{ s.id }}(data);
    
      switch (next_state) {
      case {{ namespace }}::{{ prefix_upper }}UNIMPLEMENTED:
        throw std::runtime_error("State function not fully implemented: "s + "{{ s.id_upper }}");
        break;
{% for d in s.dest_states %}      case {{ namespace }}::{{ d }}:
{% endfor %}        break;
      default:
{% if use_syslog %}        syslog(LOG_WARNING, "[FSM] Cannot pass from {{ s.id }} to %s, remaining in this state", state_names[next_state]);
{% endif %}        next_state = {{ namespace }}::{{ prefix_upper }}NO_CHANGE;
      }
{% if s.sigint_override %}      // SIGINT transition override
      if ({{ sigint }}_requested) next_state = {{ sigint_state_upper }};
{% endif %}      return next_state;
    });

{% endfor %}
{% if has_transitions %}    // Transition functions
{% for ti in transition_info %}    add_transition({{ prefix_upper }}STATE_{{ ti.first_from_upper }}, {{ prefix_upper }}STATE_{{ ti.first_to_upper }}, [](DATA_T &data) {
{% if use_syslog %}      syslog(LOG_INFO, "[FSM] State transition {{ ti.name }}");
{% endif %}      {{ ti.name }}(data);
    });

{% endfor %}{% endif %}  }

}; // class FiniteStateMachine

}; // namespace {{ namespace }}

#include "{{ cname_base }}_impl.hpp"

#endif // {{ cname_base_upper }}_HPP
)";

// C++ Source (_impl.hpp)
static const char *CPP_TEMPLATE =
    R"({% if use_syslog %}#include <syslog.h>
{% endif %}    
using namespace std;
    
// SEARCH FOR Your Code Here FOR CODE INSERTION POINTS!


namespace {{ namespace }} {

/*  ____  _        _       
 * / ___|| |_ __ _| |_ ___ 
 * \___ \| __/ _` | __/ _ \
 *  ___) | || (_| | ||  __/
 * |____/ \__\__,_|\__\___|
 *                         
 *   __                  _   _                 
 *  / _|_   _ _ __   ___| |_(_) ___  _ __  ___ 
 * | |_| | | | '_ \ / __| __| |/ _ \| '_ \/ __|
 * |  _| |_| | | | | (__| |_| | (_) | | | \__ \
 * |_|  \__,_|_| |_|\___|\__|_|\___/|_| |_|___/
 */                                             
{% for s in state_dest_info %}
// Function to be executed in state STATE_{{ s.id_upper }}
// valid return states: {{ join(s.dest_states, ", ") }}
{% if s.sigint_override %}// SIGINT triggers an emergency transition to STATE_{{ upper(sigint) }}
{% endif %}template<class T> 
{{ prefix }}state_t {{ s.function }}(T &data) {
  {{ prefix }}state_t next_state = {{ namespace }}::{{ prefix_upper }}UNIMPLEMENTED;
  /* Your Code Here */
  
  return next_state;
}
{% endfor %}

{% if has_transitions %}/*  _____                    _ _   _              
 * |_   _| __ __ _ _ __  ___(_) |_(_) ___  _ __   
 *   | || '__/ _` | '_ \/ __| | __| |/ _ \| '_ \
 *   | || | | (_| | | | \__ \ | |_| | (_) | | | | 
 *   |_||_|  \__,_|_| |_|___/_|\__|_|\___/|_| |_| 
 *                                                
 *   __                  _   _                 
 *  / _|_   _ _ __   ___| |_(_) ___  _ __  ___ 
 * | |_| | | | '_ \ / __| __| |/ _ \| '_ \/ __|
 * |  _| |_| | | | | (__| |_| | (_) | | | \__ \
 * |_|  \__,_|_| |_|\___|\__|_|\___/|_| |_|___/
 */                                              

{% for ti in transition_info %}// This function is called in {{ ti.count }} transition{% if ti.plural %}s{% endif %}:
{% for e in ti.paths %}// {{ loop.index1 }}. from {{ e.from }} to {{ e.to }}
{% endfor %}template<class T>
void {{ ti.name }}(T &data) {
  /* Your Code Here */
}

{% endfor %}{% endif %}
}; // namespace {{ namespace }}


// Example usage:
#ifdef TEST_MAIN
#include <unistd.h>
#include <thread>

struct Data {
  int count;
};

int main() {
  Data data = {1};
  auto fsm = {{ namespace }}::FiniteStateMachine(&data);
  fsm.set_timing_function([]() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  });
  fsm.run([&](Data &s) {
    std::cout << "State: " << fsm.state() << " data: " << s.count << std::endl;
  });
  return 0;
}
#endif // TEST_MAIN
)";

std::string generate_file_header(const FSM &fsm) {
  auto data = build_data(fsm);
  inja::Environment env;
  return env.render(HEADER_TEMPLATE, data);
}

std::string generate_header_h(const FSM &fsm) {
  auto data = build_data(fsm);
  inja::Environment env;
  return env.render(HEADER_TEMPLATE, data) + env.render(HH_TEMPLATE, data);
}

std::string generate_source_c(const FSM &fsm) {
  auto data = build_data(fsm);
  inja::Environment env;
  return env.render(HEADER_TEMPLATE, data) + env.render(CC_TEMPLATE, data);
}

std::string generate_header_hpp(const FSM &fsm) {
  auto data = build_data(fsm);
  inja::Environment env;
  return env.render(HEADER_TEMPLATE, data) + env.render(HPP_TEMPLATE, data);
}

std::string generate_source_cpp(const FSM &fsm) {
  auto data = build_data(fsm);
  inja::Environment env;
  return env.render(HEADER_TEMPLATE, data) + env.render(CPP_TEMPLATE, data);
}
