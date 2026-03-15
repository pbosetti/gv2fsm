#pragma once
#include "fsm.hpp"
#include <string>

/**
 * @brief Override the example TEST_MAIN template used in generated sources.
 * @param path Path to a text file containing an inja template snippet.
 *             Pass an empty path to restore the built-in example main().
 * @param error_msg Output error message populated on failure.
 * @return true when the template was loaded or reset successfully.
 */
bool set_main_template(const std::string &path, std::string *error_msg = nullptr);

/**
 * @brief Generate C header (.h) file content.
 * @param fsm Source FSM model.
 * @return Rendered C header content.
 */
std::string generate_header_h(const FSM &fsm);

/**
 * @brief Generate C source (.c) file content.
 * @param fsm Source FSM model.
 * @return Rendered C source content.
 */
std::string generate_source_c(const FSM &fsm);

/**
 * @brief Generate C++ header (.hpp) file content.
 * @param fsm Source FSM model.
 * @return Rendered C++ header content.
 */
std::string generate_header_hpp(const FSM &fsm);

/**
 * @brief Generate C++ implementation (_impl.hpp) file content.
 * @param fsm Source FSM model.
 * @return Rendered C++ implementation content.
 */
std::string generate_source_cpp(const FSM &fsm);

/**
 * @brief Generate the common file header comment shared by outputs.
 * @param fsm Source FSM model.
 * @return Rendered file header comment.
 */
std::string generate_file_header(const FSM &fsm);
