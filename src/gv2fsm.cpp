#include "gv2fsm.hpp"

#include "fsm.hpp"
#include "generator.hpp"
#include "version.hpp"

#include <algorithm>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace gv2fsm {

int run(int argc, char *argv[], std::ostream &out, std::ostream &err) {
  FSM sm;
  bool gen_header = true;
  bool gen_source = true;

  cxxopts::Options options("gv2fsm",
                           "Graphviz to Finite State Machine generator\n"
                           "Version: " +
                               std::string(GV2FSM_VERSION) +
                               "\n"
                               "See also https://github.com/pbosetti/gv2fsm");

  // clang-format off
  options.add_options()
    ("p,project", "Set the project name (in C++ also namespace)",
     cxxopts::value<std::string>())
    ("d,description", "Use DESCRIPTION string in header",
     cxxopts::value<std::string>())
    ("cpp", "Generate C++17 sources")
    ("o,output_file", "Use NAME for generated .c and .h files",
     cxxopts::value<std::string>())
    ("e,header-only", "Only generate header file")
    ("s,source-only", "Only generate source file")
    ("x,prefix", "Prepend PREFIX to names of generated functions and objects",
     cxxopts::value<std::string>())
    ("i,ino", "Generate a single .ino file (for Arduino)")
    ("l,log", "Add syslog calls in state and transition functions")
    ("k,sigint", "Install SIGINT handler that points to STATE",
     cxxopts::value<std::string>())
    ("f,force", "Overwrite existing output files")
    ("h,help", "Prints this help")
    ("dotfile", "Input .dot file", cxxopts::value<std::string>());
  // clang-format on

  options.parse_positional({"dotfile"});
  options.positional_help("scheme.dot");

  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception &e) {
    err << "ERROR: " << e.what() << "\n\n";
    err << options.help() << "\n";
    return 1;
  }

  if (result.count("help")) {
    out << options.help() << "\n";
    return 0;
  }

  if (result.count("project")) {
    std::string pn = result["project"].as<std::string>();
    std::replace(pn.begin(), pn.end(), ' ', '_');
    sm.project_name = pn;
  }

  if (result.count("description"))
    sm.description = result["description"].as<std::string>();

  if (result.count("cpp"))
    sm.plain_c = false;

  if (result.count("output_file"))
    sm.cname = result["output_file"].as<std::string>();

  if (result.count("header-only"))
    gen_source = false;

  if (result.count("source-only"))
    gen_header = false;

  if (result.count("prefix"))
    sm.prefix = result["prefix"].as<std::string>() + "_";

  if (result.count("ino"))
    sm.ino = true;

  if (result.count("log"))
    sm.syslog = true;

  if (result.count("sigint"))
    sm.sigint = result["sigint"].as<std::string>();

  bool force = result.count("force") > 0;

  if (sm.ino) {
    sm.plain_c = true;
    sm.syslog = false;
  }

  if (!result.count("dotfile")) {
    err << "ERROR: I need the path to a Graphviz file!\n\n";
    err << options.help() << "\n";
    return 1;
  }

  std::string dotpath = result["dotfile"].as<std::string>();
  namespace fs = std::filesystem;

  if (!fs::exists(dotpath) || fs::path(dotpath).extension() != ".dot") {
    err << "ERROR: " << dotpath << " does not look like a Graphviz file!\n\n";
    err << options.help() << "\n";
    return 2;
  }

  std::string error;
  if (!sm.parse(dotpath, error)) {
    err << "Error parsing the file " << dotpath << ": " << error << "\n";
    return 3;
  }

  if (sm.ino && !sm.sigint.empty()) {
    err << "ERROR: signal handler is not supported on Arduino!\n\n";
    return 4;
  }

  out << "Parsed " << sm.dotfile << "\n";
  auto top = sm.topology();
  out << "Graph topology:\n";
  out << "  Pure source nodes: ";
  for (size_t i = 0; i < top.sources.size(); i++) {
    if (i > 0)
      out << ", ";
    out << top.sources[i];
  }
  out << "\n";
  out << "  Pure sink nodes:   ";
  if (top.sinks.empty())
    out << "<none>";
  else
    for (size_t i = 0; i < top.sinks.size(); i++) {
      if (i > 0)
        out << ", ";
      out << top.sinks[i];
    }
  out << "\n";

  if (!(top.sources.size() == 1 && top.sinks.size() <= 1)) {
    out << "Topology error: there must be exactly one source and zero or one sink\n";
    return 4;
  }

  auto sl = sm.states_list();
  auto tfl = sm.transition_functions_list();

  out << "Generating " << (sm.plain_c ? "C" : "C++17")
      << " functions for states: ";
  for (size_t i = 0; i < sl.size(); i++) {
    if (i > 0)
      out << ", ";
    out << sl[i];
  }
  out << ".\n";

  out << "                       for transition: ";
  if (tfl.empty())
    out << "(none)";
  else
    for (size_t i = 0; i < tfl.size(); i++) {
      if (i > 0)
        out << ", ";
      out << tfl[i];
    }
  out << ".\n";

  if (!sm.sigint.empty()) {
    out << "Installed signal handler for SIGINT in state " << top.sources[0]
        << ":\n  stable states have emergency transition to state " << sm.sigint
        << "\n";
    bool found = false;
    for (auto &s : top.sinks)
      if (s == sm.sigint)
        found = true;
    if (!found)
      out << "WARNING: the state " << sm.sigint
          << " is not a source, please check topology\n";
  }

  if (gen_header) {
    std::string name = sm.plain_c ? sm.cname + ".h" : sm.cname + ".hpp";
    if (!force && fs::exists(name)) {
      err << "ERROR: " << name << " already exists (use -f to overwrite)\n";
      return 5;
    }
    std::ofstream f(name);
    f << (sm.plain_c ? generate_header_h(sm) : generate_header_hpp(sm));
    out << "Generated header " << name << "\n";
  }

  if (gen_source) {
    std::string name = (sm.ino || !sm.plain_c) ? sm.cname + "_impl.hpp"
                                               : sm.cname + ".c";
    if (!force && fs::exists(name)) {
      err << "ERROR: " << name << " already exists (use -f to overwrite)\n";
      return 5;
    }
    std::ofstream f(name);
    f << ((sm.ino || !sm.plain_c) ? generate_source_cpp(sm)
                                  : generate_source_c(sm));
    out << "Generated source " << name << "\n";
  }

  return 0;
}

int run(int argc, char *argv[]) { return run(argc, argv, std::cout, std::cerr); }

} // namespace gv2fsm
