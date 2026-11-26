# Changelog

User-visible changes are recorded here. No tagged GitHub release has been published as part of this repository preparation.

## Unreleased — initial 2.1.0 publication candidate

### Language and implementation

- Native C++20 lexer, Pratt parser, AST interpreter, CLI, and persistent REPL.
- Variables, nested scopes, recursive and higher-order functions, and closures.
- Integers up to 4096 bits, binary64 floats, booleans, null, Unicode strings, and mutable lists.
- Conditional branches, loops, early returns, break/continue, input/output, and builtins.
- Tracing collection of runtime objects between commands, including cycles.
- Source diagnostics, call traces, and bounded execution/formatting.
- Eight standalone `.fc` examples and a native C++ test suite.

### Repository preparation

- The WTFPL license for original project material; separate Boost notices retained.
- English and Russian README and language reference.
- CMake/CTest/CPack support alongside the existing Make build.
- Linux GCC/Clang CI, sanitizer checks, and a manually triggered package workflow.
- Contribution/security guidance, issue forms, pull-request template, and publishing instructions.

Move this section under a dated `2.1.0` heading when the release is actually published.
