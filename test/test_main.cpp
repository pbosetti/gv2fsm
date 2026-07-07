#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "dot_parser.hpp"
#include "fsm.hpp"
#include "generator.hpp"
#include "gv2fsm.hpp"
#include "merge.hpp"
#include "version.hpp"

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

// ── Helpers ──────────────────────────────────────────────────────────────────

// CMake emits SOURCE_DIR with forward slashes even on Windows; make_preferred()
// normalizes to the native separator so paths built from it stay consistent.
static const fs::path SRC_DIR  = fs::path(SOURCE_DIR).make_preferred();
// Full path to the built gv2fsm executable, provided by CMake as
// $<TARGET_FILE:gv2fsm>. It already carries the per-config subdirectory
// (e.g. "Release/") and the ".exe" suffix on multi-config generators like
// Visual Studio, which "<build>/gv2fsm" would miss.
static const fs::path GV2FSM_BIN = fs::path(GV2FSM_EXE).make_preferred();
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

static fs::path write_tmp_text(const std::string &content,
                               const std::string &stem,
                               const std::string &ext = ".txt") {
  auto timestamp = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  fs::path p = fs::temp_directory_path() / (stem + "_" + timestamp + ext);
  std::ofstream f(p);
  f << content;
  return p;
}

static int run_cmd(const std::string &cmd) {
  int rc = std::system(cmd.c_str());
#ifdef _WIN32
  return rc;
#else
  return WEXITSTATUS(rc);
#endif
}

static int run_lib(std::vector<std::string> args, std::string &out,
                   std::string &err) {
  std::vector<char *> argv;
  argv.reserve(args.size());
  for (auto &arg : args)
    argv.push_back(arg.data());

  std::ostringstream out_stream;
  std::ostringstream err_stream;
  int rc = gv2fsm::run(static_cast<int>(argv.size()), argv.data(), out_stream,
                       err_stream);
  out = out_stream.str();
  err = err_stream.str();
  return rc;
}

// The parity/smoke tests that compile generated code verify that the *output*
// is well-formed C/C++ — a property independent of the host OS. They shell out
// to clang++, so they are gated on this helper: skipped on Windows (whose
// default toolchain is not clang++, and whose builds can't provide the POSIX
// <syslog.h> the -l output pulls in) and wherever clang++ is not on PATH. The
// Linux and macOS CI legs still exercise these checks in full.
static bool can_run_compile_tests() {
#ifdef _WIN32
  return false;
#else
  return std::system("clang++ --version >/dev/null 2>&1") == 0;
#endif
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
  CHECK_THAT(hdr, ContainsSubstring("5 distinct transition functions"));
  CHECK_THAT(hdr, ContainsSubstring("6 transitions with an associated function"));
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

TEST_CASE("set_main_template overrides generated example main", "[generator]") {
  FSM fsm;
  fsm.plain_c = false;
  fsm.project_name = "sm";
  REQUIRE(parse_sm(fsm));

  fs::path main_template = write_tmp_text(
      "int main() {\n  return {{ num_states }};\n}\n", "gv2fsm_main_template",
      ".inja");

  std::string error;
  REQUIRE(set_main_template(main_template.string(), &error));
  auto cpp = generate_source_cpp(fsm);
  CHECK_THAT(cpp, ContainsSubstring("int main() {\n  return 5;\n}"));
  CHECK(cpp.find("std::this_thread::sleep_for") == std::string::npos);

  REQUIRE(set_main_template("", &error));
  fs::remove(main_template);
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

// ── Parity (C vs C++) regression tests ───────────────────────────────────────
// These exercise CLI option combinations that only broke the C++ output path
// (the C templates were already correct for all of them).

TEST_CASE("Parity: C++ with --prefix compiles", "[parity]") {
  fs::path out_dir = fs::temp_directory_path() / "gv2fsm_parity_prefix";
  fs::create_directories(out_dir);
  fs::path out_base = out_dir / "pfx";
  std::string gv2fsm = GV2FSM_BIN.string();

  std::string cmd = gv2fsm + " -p pfxns -x PFX -o " + out_base.string() +
                    " --cpp -f " + DOT_FILE.string();
  REQUIRE(run_cmd(cmd) == 0);

  if (!can_run_compile_tests()) {
    fs::remove_all(out_dir);
    SKIP("clang++ not available for the compile check");
  }

  fs::path main_src = out_dir / "main.cpp";
  {
    std::ofstream f(main_src);
    f << "#include \"pfx.hpp\"\n"
         "struct Data { int count; };\n"
         "int main() { Data d{1}; auto fsm = pfxns::FiniteStateMachine(&d); return 0; }\n";
  }
  std::string compile_cmd = "clang++ -std=c++20 -I " + out_dir.string() +
                            " -fsyntax-only " + main_src.string();
  CHECK(run_cmd(compile_cmd) == 0);
  fs::remove_all(out_dir);
}

TEST_CASE("Parity: C++ with a custom node label compiles", "[parity]") {
  fs::path out_dir = fs::temp_directory_path() / "gv2fsm_parity_label";
  fs::create_directories(out_dir);

  auto dot = write_tmp_dot(R"(digraph "custom label test" {
  init
  idle [label="custom_idle_func"]
  stop
  init -> idle
  idle -> idle
  idle -> stop
}
)",
                          "parity_label.dot");

  fs::path out_base = out_dir / "lbl";
  std::string gv2fsm = GV2FSM_BIN.string();
  std::string cmd =
      gv2fsm + " -p lblns -o " + out_base.string() + " --cpp -f " + dot.string();
  REQUIRE(run_cmd(cmd) == 0);

  auto hpp = read_all(out_base.string() + ".hpp");
  CHECK_THAT(hpp, ContainsSubstring("custom_idle_func"));

  if (!can_run_compile_tests()) {
    fs::remove(dot);
    fs::remove_all(out_dir);
    SKIP("clang++ not available for the compile check");
  }

  fs::path main_src = out_dir / "main.cpp";
  {
    std::ofstream f(main_src);
    f << "#include \"lbl.hpp\"\n"
         "struct Data { int count; };\n"
         "int main() { Data d{1}; auto fsm = lblns::FiniteStateMachine(&d); return 0; }\n";
  }
  std::string compile_cmd = "clang++ -std=c++20 -I " + out_dir.string() +
                            " -fsyntax-only " + main_src.string();
  CHECK(run_cmd(compile_cmd) == 0);
  fs::remove(dot);
  fs::remove_all(out_dir);
}

TEST_CASE("Parity: C++ with no sink state compiles", "[parity]") {
  fs::path out_dir = fs::temp_directory_path() / "gv2fsm_parity_nosink";
  fs::create_directories(out_dir);

  auto dot = write_tmp_dot(R"(digraph "no sink test" {
  init
  idle
  init -> idle
  idle -> idle
}
)",
                          "parity_nosink.dot");

  fs::path out_base = out_dir / "ns";
  std::string gv2fsm = GV2FSM_BIN.string();
  std::string cmd =
      gv2fsm + " -p nsns -o " + out_base.string() + " --cpp -f " + dot.string();
  REQUIRE(run_cmd(cmd) == 0);

  auto hpp = read_all(out_base.string() + ".hpp");
  CHECK_THAT(hpp, ContainsSubstring("while (true)"));

  if (!can_run_compile_tests()) {
    fs::remove(dot);
    fs::remove_all(out_dir);
    SKIP("clang++ not available for the compile check");
  }

  fs::path main_src = out_dir / "main.cpp";
  {
    std::ofstream f(main_src);
    f << "#include \"ns.hpp\"\n"
         "struct Data { int count; };\n"
         "int main() { Data d{1}; auto fsm = nsns::FiniteStateMachine(&d); return 0; }\n";
  }
  std::string compile_cmd = "clang++ -std=c++20 -I " + out_dir.string() +
                            " -fsyntax-only " + main_src.string();
  CHECK(run_cmd(compile_cmd) == 0);
  fs::remove(dot);
  fs::remove_all(out_dir);
}

TEST_CASE("Parity: --ino routes to the C template and produces .h/.cpp", "[parity]") {
  fs::path out_dir = fs::temp_directory_path() / "gv2fsm_parity_ino";
  fs::create_directories(out_dir);
  fs::path out_base = out_dir / "inofsm";
  std::string gv2fsm = GV2FSM_BIN.string();

  std::string cmd = gv2fsm + " -p ino -o " + out_base.string() + " --ino -f " +
                    DOT_FILE.string();
  REQUIRE(run_cmd(cmd) == 0);

  CHECK(fs::exists(out_base.string() + ".h"));
  CHECK(fs::exists(out_base.string() + ".cpp"));
  CHECK_FALSE(fs::exists(out_base.string() + "_impl.hpp"));

  auto h = read_all(out_base.string() + ".h");
  auto c = read_all(out_base.string() + ".cpp");
  CHECK_THAT(h, ContainsSubstring("arduino.h"));
  CHECK_THAT(c, ContainsSubstring("void loop()"));

  fs::remove_all(out_dir);
}

TEST_CASE("Parity: C++ header has no ODR violation across two translation units",
          "[parity]") {
  fs::path out_dir = fs::temp_directory_path() / "gv2fsm_parity_odr";
  fs::create_directories(out_dir);
  fs::path out_base = out_dir / "odrfsm";
  std::string gv2fsm = GV2FSM_BIN.string();

  std::string cmd = gv2fsm + " -p odrns -k stop -o " + out_base.string() +
                    " --cpp -f " + DOT_FILE.string();
  REQUIRE(run_cmd(cmd) == 0);

  if (!can_run_compile_tests()) {
    fs::remove_all(out_dir);
    SKIP("clang++ not available for the compile check");
  }

  fs::path tu1 = out_dir / "tu1.cpp";
  fs::path tu2 = out_dir / "tu2.cpp";
  {
    std::ofstream f(tu1);
    f << "#include \"odrfsm.hpp\"\n"
         "void touch1() { (void)odrns::state_names; (void)odrns::stop_requested; }\n";
  }
  {
    std::ofstream f(tu2);
    f << "#include \"odrfsm.hpp\"\n"
         "struct Data { int count; };\n"
         "int main() { Data d{1}; auto fsm = odrns::FiniteStateMachine(&d);\n"
         "  (void)odrns::state_names; return 0; }\n";
  }
  std::string build_cmd = "clang++ -std=c++20 -I " + out_dir.string() + " " +
                          tu1.string() + " " + tu2.string() + " -o " +
                          (out_dir / "odr_bin").string();
  CHECK(run_cmd(build_cmd) == 0);
  fs::remove_all(out_dir);
}

// ── Merge (USER CODE markers + tree-sitter legacy import) ────────────────────

static const char *MERGE_V2_CPP_DOT = R"(digraph "gv2fsm example v2" {
  init
  idle [label="do_idle"]
  setup [label="do_setup"]
  running [label="do_running"]
  paused [label="do_paused"]
  stop [label="do_stop"]

  init -> idle [label="init_to_idle"]
  idle -> idle [label="stay"]
  idle -> setup [label="to_setup"]
  setup -> running  [label="#"]
  running -> running [label="stay"]
  running -> paused [label="to_paused"]
  paused -> running [label="to_running"]
  running -> stop
}
)";

TEST_CASE("merge_generated preserves marker edits, adds new stubs, orphans removed ones",
          "[merge]") {
  FSM fsm1;
  fsm1.project_name = "sm";
  REQUIRE(parse_sm(fsm1));
  std::string fresh1 = generate_source_cpp(fsm1);

  std::string existing = fresh1;
  auto replace_once = [&](const std::string &from, const std::string &to) {
    auto pos = existing.find(from);
    REQUIRE(pos != std::string::npos);
    existing.replace(pos, from.size(), to);
  };
  replace_once("/* USER CODE BEGIN do_idle */\n  /* Your Code Here */\n\n  "
              "/* USER CODE END do_idle */",
              "/* USER CODE BEGIN do_idle */\n  next_state = STATE_SETUP;\n  "
              "/* USER CODE END do_idle */");
  replace_once(
      "/* USER CODE BEGIN stay */\n  /* Your Code Here */\n  /* USER CODE END stay */",
      "/* USER CODE BEGIN stay */\n  counter++;\n  /* USER CODE END stay */");

  auto v2_path = write_tmp_dot(MERGE_V2_CPP_DOT, "merge_v2.dot");
  FSM fsm2;
  fsm2.project_name = "sm";
  std::string err;
  REQUIRE(fsm2.parse(v2_path.string(), err));
  std::string fresh2 = generate_source_cpp(fsm2);

  auto result = merge_generated(fresh2, existing, SourceLang::Cpp);
  CHECK_FALSE(result.legacy_import);
  CHECK(result.kept == 11);
  CHECK(result.added == 3);
  CHECK(result.orphaned == 1);

  CHECK_THAT(result.text, ContainsSubstring("next_state = STATE_SETUP;"));
  CHECK_THAT(result.text, ContainsSubstring("counter++;"));
  CHECK_THAT(result.text, ContainsSubstring("do_paused"));
  CHECK_THAT(result.text, ContainsSubstring("ORPHANED to_idle"));

  fs::remove(v2_path);
}

TEST_CASE("merge_generated preserves C state body while regenerating its switch",
          "[merge]") {
  FSM fsm1;
  REQUIRE(parse_sm(fsm1));
  std::string fresh1 = generate_source_c(fsm1);

  std::string existing = fresh1;
  std::string from = "/* USER CODE BEGIN do_idle */\n  /* Your Code Here */\n\n  "
                     "/* USER CODE END do_idle */";
  auto pos = existing.find(from);
  REQUIRE(pos != std::string::npos);
  existing.replace(pos, from.size(),
                   "/* USER CODE BEGIN do_idle */\n  next_state = STATE_STOP; "
                   "// early exit\n  /* USER CODE END do_idle */");

  // v2: idle grows a direct transition straight to stop.
  auto v2_path = write_tmp_dot(R"(digraph "v2c" {
  init
  idle [label="do_idle"]
  setup [label="do_setup"]
  running [label="do_running"]
  stop [label="do_stop"]

  init -> idle [label="init_to_idle"]
  idle -> idle [label="stay"]
  idle -> setup [label="to_setup"]
  idle -> stop [label="to_stop"]
  setup -> running  [label="#"]
  running -> running [label="stay"]
  running -> idle [label="to_idle"]
  running -> stop
}
)",
                              "merge_v2c.dot");
  FSM fsm2;
  std::string err;
  REQUIRE(fsm2.parse(v2_path.string(), err));
  std::string fresh2 = generate_source_c(fsm2);

  auto result = merge_generated(fresh2, existing, SourceLang::C);
  CHECK_FALSE(result.legacy_import);
  CHECK_THAT(result.text, ContainsSubstring("next_state = STATE_STOP; // early exit"));

  auto idle_fn = result.text.find("state_t do_idle(state_data_t *data)");
  REQUIRE(idle_fn != std::string::npos);
  auto idle_switch = result.text.find("switch (next_state)", idle_fn);
  REQUIRE(idle_switch != std::string::npos);
  auto idle_fn_end = result.text.find("\n}\n", idle_switch);
  REQUIRE(idle_fn_end != std::string::npos);
  std::string idle_tail = result.text.substr(idle_switch, idle_fn_end - idle_switch);
  CHECK_THAT(idle_tail, ContainsSubstring("case STATE_STOP:"));

  fs::remove(v2_path);
}

TEST_CASE("merge_generated recovers bodies via tree-sitter when markers are absent",
          "[merge]") {
  FSM fsm;
  fsm.project_name = "sm";
  REQUIRE(parse_sm(fsm));
  std::string fresh = generate_source_cpp(fsm);

  // Simulate a file generated before markers existed by stripping the marker
  // comment lines, then hand-edit two bodies exactly as a user would --
  // inserting code and leaving the old placeholder comment in place.
  std::regex marker_line(
      R"([ \t]*/\* USER CODE (?:BEGIN|END) [A-Za-z_][A-Za-z0-9_]* \*/\n)");
  std::string existing = std::regex_replace(fresh, marker_line, "");
  REQUIRE(existing.find("USER CODE") == std::string::npos);

  {
    std::string anchor = "state_t do_idle(T &data) {\n  state_t next_state = "
                         "sm::UNIMPLEMENTED;\n";
    auto pos = existing.find(anchor);
    REQUIRE(pos != std::string::npos);
    existing.insert(pos + anchor.size(), "  next_state = STATE_SETUP;\n");
  }
  {
    std::string anchor = "void stay(T &data) {\n";
    auto pos = existing.find(anchor);
    REQUIRE(pos != std::string::npos);
    existing.insert(pos + anchor.size(), "  counter++;\n");
  }

  auto result = merge_generated(fresh, existing, SourceLang::Cpp);
  CHECK(result.legacy_import);
  CHECK_THAT(result.text, ContainsSubstring("next_state = STATE_SETUP;"));
  CHECK_THAT(result.text, ContainsSubstring("counter++;"));

  // The recovered do_idle body must not duplicate the generated prologue.
  auto count_occurrences = [&](const std::string &needle) {
    size_t n = 0, pos = 0;
    while ((pos = result.text.find(needle, pos)) != std::string::npos) {
      n++;
      pos += needle.size();
    }
    return n;
  };
  CHECK(count_occurrences("state_t next_state = sm::UNIMPLEMENTED;") == 5);
}

// ── Version ──────────────────────────────────────────────────────────────────

TEST_CASE("GV2FSM_VERSION is defined and non-empty", "[version]") {
  std::string v = GV2FSM_VERSION;
  CHECK_FALSE(v.empty());
  // Should match X.Y.Z format
  CHECK(std::count(v.begin(), v.end(), '.') == 2);
}

// ── Smoke tests ──────────────────────────────────────────────────────────────

TEST_CASE("Smoke: gv2fsm generates C++ files", "[smoke]") {
  fs::path out_dir = fs::temp_directory_path() / "gv2fsm_smoke_gen";
  fs::create_directories(out_dir);

  fs::path out_base = out_dir / "sm";
  std::string gv2fsm = GV2FSM_BIN.string();
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

TEST_CASE("Smoke: library API generates files with custom main template",
          "[smoke][library]") {
  fs::path out_dir = fs::temp_directory_path() / "gv2fsm_library_gen";
  fs::create_directories(out_dir);

  fs::path out_base = out_dir / "sm";
  fs::path main_template = write_tmp_text(
      "int main() {\n  return {{ num_states }};\n}\n", "main_template",
      ".inja");

  std::string error;
  REQUIRE(set_main_template(main_template.string(), &error));

  std::string out;
  std::string err;
  int rc = run_lib({"gv2fsm", "-p", "sm", "-o", out_base.string(), "--cpp", "-f",
                    DOT_FILE.string()},
                   out, err);
  CHECK(rc == 0);
  CHECK(err.empty());
  CHECK_THAT(out, ContainsSubstring("Generated source"));
  CHECK(fs::exists(out_base.string() + ".hpp"));
  CHECK(fs::exists(out_base.string() + "_impl.hpp"));

  auto impl = read_all(out_base.string() + "_impl.hpp");
  CHECK_THAT(impl, ContainsSubstring("int main() {\n  return 5;\n}"));

  REQUIRE(set_main_template("", &error));
  fs::remove(main_template);
  fs::remove_all(out_dir);
}

TEST_CASE("Smoke: generated C++ compiles and runs", "[smoke]") {
  // Step 1: generate into examples/ (force overwrite)
  std::string gv2fsm = GV2FSM_BIN.string();
  std::string dot = DOT_FILE.string();
  fs::path examples = SRC_DIR / "examples";
  std::string out_base = (examples / "sm").string();

  std::string gen_cmd = gv2fsm + " -p sm -o " + out_base +
                        " --cpp -k stop -l -f " + dot;
  INFO("Generate: " << gen_cmd);
  REQUIRE(run_cmd(gen_cmd) == 0);

  if (!can_run_compile_tests())
    SKIP("clang++ not available for the compile check");

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
