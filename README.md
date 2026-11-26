# Fucking C

**Одна буква. Настоящие функции. Три реализации.**

Fucking C — высокоуровневый эзотерический язык, в котором все токены исходника состоят из вариаций буквы **C**. Имена, числовые литералы и строки тоже строятся из C. Это не компилятор C, не Brainfuck и не обёртка над чем-то ещё: у языка свой лексер, Pratt-парсер, интерпретатор, типы, области видимости и функции.

В репозитории содержатся три независимые реализации языка:

| Путь | Реализация | Версия | Зависимости |
|---|---|---|---|
| [`cpp/`](cpp/) | Нативная C++20: лексер, Pratt-парсер, AST-интерпретатор, REPL, тесты | 2.1.0 | C++20-компилятор, Boost 1.74+ (header-only) |
| [`python/`](python/) | Python: лексер, парсер, интерпретатор, REPL, тесты, единый файл `fc.pyz` | 2.0.0 | Python 3.10+, без сторонних библиотек |
| [`javascript/`](javascript/) | Node.js: компактный интерпретатор ленты с набором команд из вариаций C | 1.0.0 | Node.js ≥ 18 |

## Сборка и запуск

### C++20 — [`cpp/`](cpp/)

```sh
sudo apt install g++ make libboost-dev
cd cpp
make -j2
./fc examples/factorial.fc   # 720
./fc repl
```

Или через CMake 3.20+:

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

`fc.pyz` — весь интерпретатор в одном файле, Python 3.10+, без установки зависимостей. Работает и из исходников: `python3 fc.py ...` или `python3 -m fucking_c ...`.

### Node.js — [`javascript/`](javascript/)

```sh
cd javascript
node cli.mjs examples/hello.fc
node cli.mjs examples/echo.fc --input "Привет, C!"
node --test tests/*.test.mjs
```

## Возможности языка (v2.x)

- Переменные, вложенные области видимости, присваивание.
- Именованные и анонимные функции, параметры, рекурсия, замыкания.
- Целочисленные значения до 4096 бит, конечные `float` binary64, `bool`, `null`.
- Unicode-строки и изменяемые списки.
- `if/else`, `while`, `for/in`, `break`, `continue`, `return`.
- Арифметика с приоритетами, цепные сравнения, логические операции с коротким замыканием.
- Ввод/вывод, REPL, диагностика ошибок с координатами.

Полное описание лексических правил, грамматики и семантики: [`docs/language.ru.md`](docs/language.ru.md) (по-русски) и [`docs/language.md`](docs/language.md) (по-английски).

## Лицензия

Собственный код, документация, примеры и тесты проекта выпущены под лицензией **[WTFPL](LICENSE)**.

Boost.Multiprecision, используемый в C++-реализации, остаётся под лицензией Boost Software License 1.0 — см. [`THIRD_PARTY.md`](THIRD_PARTY.md) и [`LICENSES/BSL-1.0.txt`](LICENSES/BSL-1.0.txt).


 Данный ЯП был сделан в шутку за 2 дня через вайбкод. Не воспринимайте в серьёз.
