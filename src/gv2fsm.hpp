#pragma once

#include <iosfwd>

namespace gv2fsm {

/**
 * @brief Execute the gv2fsm command-line workflow through the library API.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @param out Stream used for standard output messages.
 * @param err Stream used for error messages.
 * @return Process-style exit code.
 */
int run(int argc, char *argv[], std::ostream &out, std::ostream &err);

/**
 * @brief Execute the gv2fsm command-line workflow through the library API.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process-style exit code.
 */
int run(int argc, char *argv[]);

} // namespace gv2fsm
