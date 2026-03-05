#pragma once
#include "fsm.hpp"
#include <string>

// Generate C header (.h) file content
std::string generate_header_h(const FSM &fsm);

// Generate C source (.c) file content
std::string generate_source_c(const FSM &fsm);

// Generate C++ header (.hpp) file content
std::string generate_header_hpp(const FSM &fsm);

// Generate C++ source (_impl.hpp) file content
std::string generate_source_cpp(const FSM &fsm);

// Generate the common file header comment
std::string generate_file_header(const FSM &fsm);
