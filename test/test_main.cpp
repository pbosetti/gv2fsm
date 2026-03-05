#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "dot_parser.hpp"
#include "fsm.hpp"
#include "generator.hpp"
#include "version.hpp"

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

// ── Helpers ──────────────────────────────────────────────────────────────────

static const fs::path SRC_DIR  = SOURCE_DIR;   // set by CMake
static const fs::path BIN_DIR  = BINARY_DIR;   // set by CMake
static const fs::path DOT_FILE = SRC_DIR / "examples" / "sm.dot";
static const fs::path SIMPLE_DOT = SRC_DIR / "examples" / "simple.dot";

static std::string read_all(const fs::path &p) {
  std::ifstream f(p);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Parse the example sm.dot into an FSM, returning success
static bool parse_sm(FSM &fsm, const std::string &dotpath = DOT_FILE.string()) {
  std::string err;
  return fsm.parse(dotpath, err);
}

// Write a string to a temp file and return its path
static fs::path write_tmp_dot(const std::string &content,
                              const std::string &name = "tmp_test.dot") {
  fs::path p = fs::temp_directory_path() / name;
  std::ofstream f(p);
  f << content;
  return p;
}

// ── DotParser unit tests ─────────────────────────────────────────────────────

TEST_CASE("DotParser: parse valid digraph", "[parser]") {
  DotGraph g;
  std::string err;
  REQUIRE(parse_dot_file(DOT_FILE.string(), g, err));
  CHECK(g.directed);
  CHECK(g.name == "gv2fsm example");
  CHECK(g.nodes.size() == 5);
  CHECK(g.edges.size() == 7);
}

TEST_CASE("DotParser: node labels", "[parser]") {
  DotGraph g;
  std::string err;
  REQUIRE(parse_dot_file(DOT_FILE.string(), g, err));

  // init has no label attribute
  CHECK(g.nodes[0].id == "init");
  CHECK(g.nodes[0].label.empty());

  // idle has explicit label
  CHECK(g.nodes[1].id == "idle");
  CHECK(g.nodes[1].label == "do_idle");
}

TEST_CASE("DotParser: edge labels including '#'", "[parser]") {
  DotGraph g;
  std::string err;
  REQUIRE(parse_dot_file(DOT_FILE.string(), g, err));

  // First edge: init -> idle with label "init_to_idle"
  CHECK(g.edges[0].from == "init");
  CHECK(g.edges[0].to == "idle");
  CHECK(g.edges[0].label == "init_to_idle");

  // setup -> running has '#' auto-gen label
  CHECK(g.edges[3].from == "setup");
  CHECK(g.edges[3].to == "running");
  CHECK(g.edges[3].label == "#");

  // running -> stop has no label
  CHECK(g.edges[6].from == "running");
  CHECK(g.edges[6].to == "stop");
  CHECK(g.edges[6].label.empty());
}

TEST_CASE("DotParser: reject undirected graph", "[parser]") {
  auto p = write_tmp_dot("graph test { a -- b }");
  DotGraph g;
  std::string err;
  CHECK_FALSE(parse_dot_file(p.string(), g, err));
  CHECK_THAT(err, ContainsSubstring("not directed"));
  fs::remove(p);
}

TEST_CASE("DotParser: reject empty graph", "[parser]") {
  auto p = write_tmp_dot("digraph empty {}");
  DotGraph g;
  std::string err;
  CHECK_FALSE(parse_dot_file(p.string(), g, err));
  CHECK_THAT(err, ContainsSubstring("empty"));
  fs::remove(p);
}

TEST_CASE("DotParser: missing file", "[parser]") {
  DotGraph g;
  std::string err;
  CHECK_FALSE(parse_dot_file("/nonexistent/file.dot", g, err));
  CHECK_THAT(err, ContainsSubstring("Cannot open"));
}

// ── FSM::parse unit tests ────────────────────────────────────────────────────

TEST_CASE("FSM::parse populates states", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  REQUIRE(fsm.states.size() == 5);
  CHECK(fsm.states[0].id == "init");
  CHECK(fsm.states[0].function == "do_init");  // no explicit label -> do_ prefix
  CHECK(fsm.states[1].id == "idle");
  CHECK(fsm.states[1].function == "do_idle");   // explicit label
  CHECK(fsm.states[4].id == "stop");
}

TEST_CASE("FSM::parse populates transitions", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  REQUIRE(fsm.transitions.size() == 7);

  // '#' auto-generated: setup -> running becomes "setup_to_running"
  auto &t3 = fsm.transitions[3];
  CHECK(t3.from == "setup");
  CHECK(t3.to == "running");
  CHECK(t3.function == "setup_to_running");

  // Empty label -> empty function
  auto &t6 = fsm.transitions[6];
  CHECK(t6.from == "running");
  CHECK(t6.to == "stop");
  CHECK(t6.function.empty());
}

TEST_CASE("FSM::parse with prefix", "[fsm]") {
  FSM fsm;
  fsm.prefix = "MY_";
  REQUIRE(parse_sm(fsm));
  CHECK(fsm.states[0].function == "MY_do_init");
  CHECK(fsm.transitions[0].function == "MY_init_to_idle");
}

TEST_CASE("FSM::parse sets cname from filename", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  CHECK(fsm.cname == "sm");
}

TEST_CASE("FSM::parse sets description from graph name", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  CHECK(fsm.description == "gv2fsm example");
}

TEST_CASE("FSM::parse rejects invalid sigint state", "[fsm]") {
  FSM fsm;
  fsm.sigint = "nonexistent";
  std::string err;
  CHECK_FALSE(fsm.parse(DOT_FILE.string(), err));
  CHECK_THAT(err, ContainsSubstring("Missing SIGINT state"));
}

// ── FSM accessor methods ─────────────────────────────────────────────────────

TEST_CASE("FSM::states_list", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  auto sl = fsm.states_list();
  REQUIRE(sl.size() == 5);
  CHECK(sl[0] == "init");
  CHECK(sl[4] == "stop");
}

TEST_CASE("FSM::state_functions_list", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  auto sfl = fsm.state_functions_list();
  REQUIRE(sfl.size() == 5);
  CHECK(sfl[0] == "do_init");
  CHECK(sfl[1] == "do_idle");
}

TEST_CASE("FSM::transition_functions_list is unique", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  auto tfl = fsm.transition_functions_list();
  // sm.dot has: init_to_idle, stay, to_setup, setup_to_running, to_idle
  CHECK(tfl.size() == 5);
  // "stay" appears twice in edges (idle->idle, running->running) but only once here
  CHECK(std::count(tfl.begin(), tfl.end(), "stay") == 1);
}

TEST_CASE("FSM::transitions_map dimensions and content", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  auto tm = fsm.transitions_map();
  REQUIRE(tm.size() == 5);
  for (auto &row : tm)
    REQUIRE(row.size() == 5);

  // init(0) -> idle(1) = "init_to_idle"
  CHECK(tm[0][1] == "init_to_idle");
  // init(0) -> init(0) = "NULL" (no self-loop)
  CHECK(tm[0][0] == "NULL");
  // running(3) -> stop(4) = "NULL" (edge with no label)
  CHECK(tm[3][4] == "NULL");
}

TEST_CASE("FSM::destinations", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  auto dest = fsm.destinations();

  CHECK(dest["init"].size() == 1);
  CHECK(dest["init"][0] == "idle");

  CHECK(dest["idle"].size() == 2);  // idle, setup
  CHECK(dest["running"].size() == 3); // running, idle, stop

  CHECK(dest["stop"].empty());
}

TEST_CASE("FSM::transitions_paths", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  auto tp = fsm.transitions_paths();

  CHECK(tp.count("init_to_idle") == 1);
  CHECK(tp["init_to_idle"].size() == 1);
  CHECK(tp["init_to_idle"][0].from == "init");
  CHECK(tp["init_to_idle"][0].to == "idle");

  // "stay" has two paths: idle->idle and running->running
  CHECK(tp["stay"].size() == 2);
}

TEST_CASE("FSM::topology", "[fsm]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));
  auto top = fsm.topology();
  REQUIRE(top.sources.size() == 1);
  CHECK(top.sources[0] == "init");
  REQUIRE(top.sinks.size() == 1);
  CHECK(top.sinks[0] == "stop");
}

TEST_CASE("FSM::topology on simple graph", "[fsm]") {
  FSM fsm;
  std::string err;
  REQUIRE(fsm.parse(SIMPLE_DOT.string(), err));
  auto top = fsm.topology();
  REQUIRE(top.sources.size() == 1);
  CHECK(top.sources[0] == "init");
  REQUIRE(top.sinks.size() == 1);
  CHECK(top.sinks[0] == "stop");
}

// ── Generator unit tests ─────────────────────────────────────────────────────

TEST_CASE("generate_file_header contains metadata", "[generator]") {
  FSM fsm;
  fsm.project_name = "TestProj";
  fsm.description = "Test description";
  REQUIRE(parse_sm(fsm));

  auto hdr = generate_file_header(fsm);
  CHECK_THAT(hdr, ContainsSubstring("TestProj"));
  CHECK_THAT(hdr, ContainsSubstring("Test description"));
  CHECK_THAT(hdr, ContainsSubstring(GV2FSM_VERSION));
  CHECK_THAT(hdr, ContainsSubstring("5 states"));
  CHECK_THAT(hdr, ContainsSubstring("5 transition functions"));
}

TEST_CASE("generate_header_h produces valid C header", "[generator]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));

  auto h = generate_header_h(fsm);
  CHECK_THAT(h, ContainsSubstring("#ifndef SM_H"));
  CHECK_THAT(h, ContainsSubstring("#define SM_H"));
  CHECK_THAT(h, ContainsSubstring("#endif"));
  CHECK_THAT(h, ContainsSubstring("STATE_INIT"));
  CHECK_THAT(h, ContainsSubstring("STATE_STOP"));
  CHECK_THAT(h, ContainsSubstring("do_init"));
  CHECK_THAT(h, ContainsSubstring("NUM_STATES"));
}

TEST_CASE("generate_source_c produces valid C source", "[generator]") {
  FSM fsm;
  REQUIRE(parse_sm(fsm));

  auto c = generate_source_c(fsm);
  CHECK_THAT(c, ContainsSubstring("#include \"sm.h\""));
  CHECK_THAT(c, ContainsSubstring("run_state"));
  CHECK_THAT(c, ContainsSubstring("state_names"));
}

TEST_CASE("generate_header_hpp produces valid C++ header", "[generator]") {
  FSM fsm;
  fsm.plain_c = false;
  fsm.project_name = "sm";
  REQUIRE(parse_sm(fsm));

  auto hpp = generate_header_hpp(fsm);
  CHECK_THAT(hpp, ContainsSubstring("#ifndef SM_HPP"));
  CHECK_THAT(hpp, ContainsSubstring("#define SM_HPP"));
  CHECK_THAT(hpp, ContainsSubstring("#endif // SM_HPP"));
  CHECK_THAT(hpp, ContainsSubstring("namespace sm"));
  CHECK_THAT(hpp, ContainsSubstring("FiniteStateMachine"));
  CHECK_THAT(hpp, ContainsSubstring("STATE_INIT"));
  CHECK_THAT(hpp, ContainsSubstring("#include \"sm_impl.hpp\""));
}

TEST_CASE("generate_source_cpp produces valid C++ impl", "[generator]") {
  FSM fsm;
  fsm.plain_c = false;
  fsm.project_name = "sm";
  REQUIRE(parse_sm(fsm));

  auto cpp = generate_source_cpp(fsm);
  CHECK_THAT(cpp, ContainsSubstring("do_init"));
  CHECK_THAT(cpp, ContainsSubstring("do_idle"));
  CHECK_THAT(cpp, ContainsSubstring("UNIMPLEMENTED"));
}

TEST_CASE("generate_header_hpp with prefix", "[generator]") {
  FSM fsm;
  fsm.plain_c = false;
  fsm.prefix = "PFX_";
  fsm.project_name = "test";
  REQUIRE(parse_sm(fsm));

  auto hpp = generate_header_hpp(fsm);
  CHECK_THAT(hpp, ContainsSubstring("PFX_STATE_INIT"));
  CHECK_THAT(hpp, ContainsSubstring("PFX_do_init"));
}

TEST_CASE("generate_header_hpp with syslog", "[generator]") {
  FSM fsm;
  fsm.plain_c = false;
  fsm.syslog = true;
  fsm.project_name = "sm";
  REQUIRE(parse_sm(fsm));

  auto hpp = generate_header_hpp(fsm);
  CHECK_THAT(hpp, ContainsSubstring("syslog"));
}

TEST_CASE("generate_header_hpp with sigint", "[generator]") {
  FSM fsm;
  fsm.plain_c = false;
  fsm.sigint = "stop";
  fsm.project_name = "sm";
  REQUIRE(parse_sm(fsm));

  auto hpp = generate_header_hpp(fsm);
  CHECK_THAT(hpp, ContainsSubstring("SIGINT"));
  CHECK_THAT(hpp, ContainsSubstring("stop_requested"));
}

TEST_CASE("generate_header_hpp include guard uses basename", "[generator]") {
  FSM fsm;
  fsm.plain_c = false;
  fsm.cname = "path/to/myfsm";
  fsm.project_name = "test";
  REQUIRE(parse_sm(fsm));

  auto hpp = generate_header_hpp(fsm);
  CHECK_THAT(hpp, ContainsSubstring("#ifndef MYFSM_HPP"));
  CHECK_THAT(hpp, ContainsSubstring("#include \"myfsm_impl.hpp\""));
  // Must NOT contain the path in guard
  CHECK(hpp.find("PATH/TO/MYFSM_HPP") == std::string::npos);
}

TEST_CASE("generate_header_h include guard uses basename", "[generator]") {
  FSM fsm;
  fsm.cname = "some/dir/output";
  REQUIRE(parse_sm(fsm));

  auto h = generate_header_h(fsm);
  CHECK_THAT(h, ContainsSubstring("#ifndef OUTPUT_H"));
  CHECK(h.find("SOME/DIR/OUTPUT_H") == std::string::npos);
}

TEST_CASE("generate_source_c include uses basename", "[generator]") {
  FSM fsm;
  fsm.cname = "some/dir/output";
  REQUIRE(parse_sm(fsm));

  auto c = generate_source_c(fsm);
  CHECK_THAT(c, ContainsSubstring("#include \"output.h\""));
}

// ── Version ──────────────────────────────────────────────────────────────────

TEST_CASE("GV2FSM_VERSION is defined and non-empty", "[version]") {
  std::string v = GV2FSM_VERSION;
  CHECK_FALSE(v.empty());
  // Should match X.Y.Z format
  CHECK(std::count(v.begin(), v.end(), '.') == 2);
}

// ── Smoke tests ──────────────────────────────────────────────────────────────

static int run_cmd(const std::string &cmd) {
  int rc = std::system(cmd.c_str());
#ifdef _WIN32
  return rc;
#else
  return WEXITSTATUS(rc);
#endif
}

TEST_CASE("Smoke: gv2fsm generates C++ files", "[smoke]") {
  fs::path out_dir = fs::temp_directory_path() / "gv2fsm_smoke_gen";
  fs::create_directories(out_dir);

  fs::path out_base = out_dir / "sm";
  std::string gv2fsm = (BIN_DIR / "gv2fsm").string();
  std::string dot = DOT_FILE.string();

  // Remove any previous output
  fs::remove(fs::path(out_base.string() + ".hpp"));
  fs::remove(fs::path(out_base.string() + "_impl.hpp"));

  std::string cmd = gv2fsm + " -p sm -o " + out_base.string() +
                    " --cpp -k stop -l " + dot;
  INFO("Command: " << cmd);
  int rc = run_cmd(cmd);
  CHECK(rc == 0);

  CHECK(fs::exists(out_base.string() + ".hpp"));
  CHECK(fs::exists(out_base.string() + "_impl.hpp"));

  // Verify content sanity
  auto hpp = read_all(out_base.string() + ".hpp");
  CHECK_THAT(hpp, ContainsSubstring("#ifndef SM_HPP"));
  CHECK_THAT(hpp, ContainsSubstring("namespace sm"));
  CHECK_THAT(hpp, ContainsSubstring("#include \"sm_impl.hpp\""));

  fs::remove_all(out_dir);
}

TEST_CASE("Smoke: generated C++ compiles and runs", "[smoke]") {
  // Step 1: generate into examples/ (force overwrite)
  std::string gv2fsm = (BIN_DIR / "gv2fsm").string();
  std::string dot = DOT_FILE.string();
  fs::path examples = SRC_DIR / "examples";
  std::string out_base = (examples / "sm").string();

  std::string gen_cmd = gv2fsm + " -p sm -o " + out_base +
                        " --cpp -k stop -l -f " + dot;
  INFO("Generate: " << gen_cmd);
  REQUIRE(run_cmd(gen_cmd) == 0);

  // Step 2: compile examples/main.cpp
  fs::path main_src = examples / "main.cpp";
  fs::path main_bin = examples / "main_smoke_test";
  std::string compile_cmd = "clang++ " + main_src.string() +
                            " -o " + main_bin.string() + " -std=c++20";
  INFO("Compile: " << compile_cmd);
  REQUIRE(run_cmd(compile_cmd) == 0);
  REQUIRE(fs::exists(main_bin));

  // Step 3: run — should return 1 (UNIMPLEMENTED state throws)
  int rc = run_cmd(main_bin.string());
  CHECK(rc == 1);

  fs::remove(main_bin);
}
