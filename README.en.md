# Fucking C

**One letter. Real functions. Three implementations.**

Fucking C is a high-level esoteric programming language whose source tokens are all variations of the letter **C**. Names, number literals, and strings are built from C as well. It is not a C compiler, not Brainfuck, and not a wrapper around anything: the language has its own lexer, Pratt parser, interpreter, types, scopes, and functions.

The repository contains three independent implementations of the language:

| Path | Implementation | Version | Requirements |
|---|---|---|---|
| [`cpp/`](cpp/) | Native C++20: lexer, Pratt parser, AST interpreter, REPL, tests | 2.1.0 | C++20 compiler, Boost 1.74+ (header-only) |
| [`python/`](python/) | Python: lexer, parser, interpreter, REPL, tests, single-file `fc.pyz` | 2.0.0 | Python 3.10+, no third-party libraries |
| [`javascript/`](javascript/) | Node.js: compact tape interpreter with a command set built from C variations | 1.0.0 | Node.js ≥ 18 |

## Build and run

### C++20 — [`cpp/`](cpp/)

```sh
sudo apt install g++ make libboost-dev
cd cpp
make -j2
./fc examples/factorial.fc   # 720
./fc repl
```

Or with CMake 3.20+:

```sh
cd cpp
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --output-on-failure
build/cmake/fc examples/map.fc
```

### Python — [`python/`](python/)

```sh
cd python
python3 fc.pyz examples/factorial.fc   # 720
python3 fc.pyz examples/map.fc        # [1, 4, 9, 16]
python3 fc.pyz repl
python3 -m unittest discover -s tests
```

`fc.pyz` is the whole interpreter in one file: Python 3.10+, no installation required. It also runs from source: `python3 fc.py ...` or `python3 -m fucking_c ...`.

### Node.js — [`javascript/`](javascript/)

```sh
cd javascript
node cli.mjs examples/hello.fc
node cli.mjs examples/echo.fc --input "Hello, C!"
node --test tests/*.test.mjs
```

## Language features (v2.x)

- Variables, nested scopes, assignment.
- Named and anonymous functions, parameters, recursion, closures.
- Integers up to 4096 bits, finite `float` binary64, `bool`, `null`.
- Unicode strings and mutable lists.
- `if/else`, `while`, `for/in`, `break`, `continue`, `return`.
- Arithmetic with precedence, chained comparisons, short-circuit logic.
- Input/output, REPL, error diagnostics with coordinates.

The full lexical rules, grammar, and semantics are documented in [`docs/language.md`](docs/language.md) (English) and [`docs/language.ru.md`](docs/language.ru.md) (Russian).

## License

Original source code, documentation, examples, and tests are released under the **[WTFPL](LICENSE)**.

Boost.Multiprecision, used by the C++ implementation, remains under the Boost Software License 1.0 — see [`THIRD_PARTY.md`](THIRD_PARTY.md) and [`LICENSES/BSL-1.0.txt`](LICENSES/BSL-1.0.txt).
