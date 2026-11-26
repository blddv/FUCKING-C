// SPDX-License-Identifier: WTFPL
#include "fucking_c.hpp"

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    const bool interactive = _isatty(_fileno(stdin)) != 0;
#else
    const bool interactive = isatty(STDIN_FILENO) != 0;
#endif
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
    return fc::cli_main(std::move(arguments), std::cin, std::cout, std::cerr, interactive);
}
