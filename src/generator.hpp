#pragma once
#include "fsm.hpp"
#include <string>

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
