// SPDX-License-Identifier: WTFPL
#include "fucking_c.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message = "Проверка не выполнена") {
    if (!condition) throw std::runtime_error(message);
}
void equal(const std::string& actual, const std::string& expected) {
    require(actual == expected, "Ожидалось: [" + expected + "]; получено: [" + actual + "]");
}
std::string value(const std::string& source) { return fc::capture(source + " ċ").value; }
std::string number(long long n) { return fc::encode_int(std::to_string(n)); }
void error(const std::function<void()>& action, fc::Category category = fc::Category::runtime) {
    try { action(); }
    catch (const fc::Error& caught) { require(caught.category() == category, caught.diagnostic()); return; }
    throw std::runtime_error("Ожидалась ошибка языка");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Нет файла " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
struct Suite {
    int passed = 0, failed = 0;
    void test(const std::string& name, const std::function<void()>& action) {
        try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const fc::Error& caught) { ++failed; std::cerr << "FAIL " << name << "\n" << caught.diagnostic() << '\n'; }
        catch (const std::exception& caught) { ++failed; std::cerr << "FAIL " << name << "\n" << caught.what() << '\n'; }
    }
};
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() / ("fc-cpp-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(path), "Не удалось создать временную папку");
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    std::string file(const std::string& name, const std::string& text) const {
        const auto destination = path / name;
        std::ofstream output(destination, std::ios::binary); output << text;
        require(static_cast<bool>(output), "Не удалось записать тестовый файл");
        return destination.string();
    }
};
struct CLIResult { int code; std::string output, errors; };
CLIResult cli(std::vector<std::string> args, const std::string& input = "") {
    std::istringstream in(input); std::ostringstream out, err;
    const int code = fc::cli_main(std::move(args), in, out, err);
    return {code, out.str(), err.str()};
}

} // namespace

int main(int argc, char** argv) {
    Suite suite;
    const auto examples = std::filesystem::absolute(argc > 1 ? argv[1] : "examples");
    TemporaryDirectory temporary;

    suite.test("distinct C symbols", [] {
        for (const auto& [symbol, meaning] : fc::syntax_symbols()) {
            require(!meaning.empty()); require(fc::scan(symbol).size() == 2);
        }
    });
    suite.test("UTF-8 locations, tabs and CRLF", [] {
        const auto tokens = fc::scan("C\tƈC\r\n  Ç ƇC\rċ");
        require(tokens[0].line == 1 && tokens[0].column == 1);
        require(tokens[1].column == 3);
        require(tokens[2].line == 2 && tokens[2].column == 3);
        require(tokens[4].line == 3 && tokens[4].column == 1);
    });
    suite.test("BOM and Unicode line separators", [] {
        const auto tokens = fc::scan("﻿C ƈC Ç ƇC ċ");
        require(tokens[0].kind == "Let" && tokens[1].line == 2 && tokens[2].line == 3);
    });
    suite.test("decomposed accents and Cyrillic C aliases", [] {
        equal(fc::capture("С ƈС Ç ƇСС с̇ С̶ С́ ƈС с́ с̇").output, "3\n");
        equal(value("C̆"), "true");
    });
    suite.test("unknown characters and comments rejected", [] {
        for (const std::string text : {"let", "123", "(", "# comment", "// comment", "CƈC", "Ƈ", "ƈ", "©", "CCCC"})
            error([&] { (void)fc::scan(text); }, fc::Category::syntax);
    });
    suite.test("invalid UTF-8 rejected", [] {
        for (const auto& bytes : std::vector<std::string>{"\xff", "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc2"})
            error([&] { (void)fc::scan(bytes); }, fc::Category::syntax);
    });
    suite.test("integer literal helpers", [] {
        equal(fc::encode_int("42"), "ƇCcCcCc");
        for (long long n : {0LL, 1LL, 2LL, 42LL, -42LL, 9223372036854775807LL}) equal(value(number(n)), std::to_string(n));
        equal(value(fc::encode_int("1267650600228229401496703205376")), "1267650600228229401496703205376");
    });
    suite.test("string literal helpers", [] {
        const std::vector<std::string> cases{"", "C", "Hello, C!", "Привет 🦊", "汉字", std::string("\0\t\r\n", 4), "\"\\"};
        for (const auto& text : cases) equal(value(fc::encode_string(text)), text);
        equal(fc::encode_string(""), "Ȼ");
    });
    suite.test("string literals require complete valid UTF-8 bytes", [] {
        for (const std::string text : {"ȻC", "ȻCCCCCCCC", "ȻCccccccc"}) error([&] { (void)fc::scan(text); }, fc::Category::syntax);
    });
    suite.test("identifier helper", [] {
        for (const std::string name : {"counter", "имя", "C", "a_b"}) require(fc::scan(fc::encode_name(name))[0].kind == "Ident");
        error([] { (void)fc::encode_name(""); }, fc::Category::syntax);
    });
    suite.test("float literals and subnormals", [] {
        equal(value(fc::encode_float(0.0)), "0.0");
        equal(value(fc::encode_float(-0.0)), "-0.0");
        equal(value(fc::encode_float(.1)), "0.1");
        equal(value(fc::encode_float(-2.5)), "-2.5");
        equal(value(fc::encode_float(1e308)), "1e+308");
        equal(value(fc::encode_float(std::numeric_limits<double>::denorm_min())), "5e-324");
        error([] { (void)fc::encode_float(std::numeric_limits<double>::infinity()); }, fc::Category::syntax);
    });
    suite.test("source and literal size limits", [] {
        error([] { (void)fc::scan(std::string(1'000'001, ' ')); }, fc::Category::limit);
        error([] { (void)fc::scan("Ƈ" + std::string(4097, 'C')); }, fc::Category::limit);
    });
    suite.test("diagnostics preserve filename and source coordinates", [] {
        try { (void)fc::parse("C ƈC Ç ƇC ċ\n  ???", "bad.fc"); }
        catch (const fc::Error& caught) {
            require(caught.site().line == 2 && caught.site().column == 3);
            require(caught.diagnostic().find("bad.fc:2:3") != std::string::npos);
            require(caught.diagnostic().find('^') != std::string::npos); return;
        }
        throw std::runtime_error("Ожидалась ошибка");
    });
    suite.test("parser creates executable AST", [] {
        const auto program = fc::parse("C ƈC Ç ƇC C̄ ƇCc C̈ ƇCC ċ ƈC ċ");
        require(program.statement_count() == 2 && program.ends_with_expression());
        equal(fc::capture("C ƈC Ç ƇC C̄ ƇCc C̈ ƇCC ċ ƈC ċ").value, "7");
    });
    suite.test("empty program", [] { require(fc::parse(" \n").statement_count() == 0); equal(fc::capture("").output, ""); });
    suite.test("semicolons and delimiters", [] {
        for (const std::string source : {"C ƈC Ç ƇC", "Ĉ", "C̶ Ć ƇC ċ", "Č ƇC ċ", "ĉ", "Ć ć ċ", "CC ƇC Ĉ ĉ"})
            error([&] { (void)fc::parse(source); }, fc::Category::syntax);
    });
    suite.test("return, break and continue context", [] {
        for (const std::string source : {"cC ƇC ċ", "CcC ċ", "Ccc ċ", "Cc Ć C̆ ć Ĉ c ƈC Ć ć Ĉ CcC ċ ĉ ĉ"})
            error([&] { (void)fc::parse(source); }, fc::Category::syntax);
    });
    suite.test("duplicate parameters and invalid lvalues", [] {
        for (const std::string source : {"c ƈC Ć ƈc Ċ ƈc ć Ĉ ĉ", "ƇC Ç ƇCc ċ", "Ć ƇC C̄ ƇC ć Ç ƇCc ċ"})
            error([&] { (void)fc::parse(source); }, fc::Category::syntax);
    });
    suite.test("trailing commas", [] {
        equal(fc::capture("c ƈC Ć ƈc Ċ ć Ĉ cC ƈc ċ ĉ C̶ Ć ƈC Ć Č ƇC Ċ č Ċ ć Ċ ć ċ").output, "[1]\n");
    });
    suite.test("deep syntax and flat AST chains are bounded", [] {
        std::string source;
        for (int i = 0; i < 2000; ++i) source += "Ć ";
        source += "ƇC "; for (int i = 0; i < 2000; ++i) source += "ć "; source += "ċ";
        error([&] { (void)fc::parse(source); }, fc::Category::limit);
        source = "ƇC"; for (int i = 0; i < 1000; ++i) source += " C̄ ƇC"; source += " ċ";
        error([&] { (void)fc::parse(source); }, fc::Category::limit);
    });
    suite.test("arithmetic precedence and associativity", [] {
        const std::vector<std::pair<std::string, std::string>> cases{
            {"ƇCc C̄ ƇCC C̈ ƇCcc", "14"}, {"Ć ƇCc C̄ ƇCC ć C̈ ƇCcc", "20"},
            {"ƇCcCc c̄ ƇCC c̄ ƇCc", "5"}, {"ƇCCC c̈ ƇCc", "3.5"}, {"c̄ ƇCCC c̃ ƇCc", "-4"},
            {"c̄ ƇCCC C̃ ƇCc", "1"}, {"ƇCc C̈C ƇCC C̈C ƇCc", "512"}, {"c̄ ƇCc C̈C ƇCc", "-4"},
            {"Ć c̄ ƇCc ć C̈C ƇCc", "4"}, {"ƇCc C̈C c̄ ƇCc", "0.25"}
        };
        for (const auto& [source, expected] : cases) equal(value(source), expected);
    });
    suite.test("integers are not bytes or restricted to int64", [] {
        equal(value("ƇCCCCCCCC C̄ ƇC"), "256");
        equal(value(fc::encode_int("1267650600228229401496703205376") + " C̈ ƇCc"), "2535301200456458802993406410752");
    });
    suite.test("large int/float comparisons are exact", [] {
        const auto n = fc::encode_int("9007199254740993");
        const auto f = fc::encode_float(9007199254740992.0);
        equal(value(n + " ÇÇ " + f), "false"); equal(value(n + " c̲ " + f), "true");
        equal(value(f + " C̲ " + n), "true");
    });
    suite.test("comparison chains short circuit", [] {
        equal(value("ƇC C̲ ƇCc C̲C ƇCC"), "true");
        equal(value("ƇCc C̲ ƇC C̲ ƈC"), "false");
        equal(value("C̆ ÇÇ ƇC"), "false"); equal(value("C̊ ÇÇ C̊"), "true");
        equal(value("ƇC ÇÇ " + fc::encode_float(1.0)), "true");
    });
    suite.test("logical operations return operands", [] {
        equal(value("c̆ cCC ƈC"), "false"); equal(value("C̆ cCc ƈC"), "true");
        equal(value("Ƈc cCc ƇCc"), "2"); equal(value("ƇCc cCC ƇCC"), "3");
        equal(value("ccC ƇC ÇÇ ƇCc"), "true"); equal(value("ccC C̆ cCc c̆"), "false");
    });
    suite.test("all primitive types", [] {
        const std::vector<std::pair<std::string, std::string>> cases{{"ƇC", "int"}, {fc::encode_float(1.5), "float"}, {"C̆", "bool"}, {"C̊", "null"}, {"Ȼ", "str"}, {"Č č", "list"}, {"C̶", "function"}};
        for (const auto& [source, expected] : cases) equal(value("C̶CCc Ć " + source + " ć"), expected);
    });
    suite.test("unary and binary type errors", [] {
        for (const std::string source : {"C̆ C̄ ƇC", "ƇC C̄ C̊", "C̊ c̄ ƇC", "c̄ Ȼ", "Ȼ C̄ ƇC", "C̆ C̲ ƇCc"}) error([&] { (void)value(source); });
    });
    suite.test("zero division and complex results rejected", [] {
        for (const std::string source : {"ƇC c̈ Ƈc", "ƇC c̃ Ƈc", "ƇC C̃ Ƈc", "Ƈc C̈C c̄ ƇC", "Ć c̄ ƇC ć C̈C Ć ƇC c̈ ƇCc ć"}) error([&] { (void)value(source); });
    });
    suite.test("Unicode strings, concatenation, repetition and indexing", [] {
        const auto hello = fc::encode_string("Привет");
        equal(value(hello + " C̄ " + fc::encode_string(" 🦊")), "Привет 🦊");
        equal(value(fc::encode_string("я") + " C̈ ƇCC"), "яяя");
        equal(value("ƇCc C̈ " + fc::encode_string("ab")), "abab");
        equal(value("C̶C Ć " + fc::encode_string("🦊я") + " ć"), "2");
        equal(value(hello + " Č c̄ ƇC č"), "т"); equal(value(hello + " Č Ƈc č"), "П");
    });
    suite.test("lists and structural equality", [] {
        equal(value("Č ƇC Ċ C̆ Ċ C̊ č"), "[1, true, null]");
        equal(value("Č ƇC č C̄ Č ƇCc č"), "[1, 2]");
        equal(value("Č ƇC č C̈ ƇCC"), "[1, 1, 1]");
        equal(value("Č ƇC č Č c̄ ƇC č"), "1");
        equal(value("Č ƇC Ċ Č ƇCc č č ÇÇ Č ƇC Ċ Č ƇCc č č"), "true");
    });
    suite.test("index bounds and types", [] {
        for (const std::string source : {"Č č Č Ƈc č", "Ȼ Č Ƈc č", "ƇC Č Ƈc č", "Č ƇC č Č C̆ č", "Č ƇC č Č ƇCc č"}) error([&] { (void)value(source); });
    });
    suite.test("200 deterministic arithmetic trees", [] {
        std::mt19937 random(7331);
        std::function<std::pair<std::string, long long>(int)> tree = [&](int depth) -> std::pair<std::string, long long> {
            if (!depth || random() % 4 == 0) { const long long n = static_cast<int>(random() % 41) - 20; return {number(n), n}; }
            auto [left, a] = tree(depth - 1); auto [right, b] = tree(depth - 1);
            const auto operation = random() % 3;
            const std::string symbol = operation == 0 ? "C̄" : operation == 1 ? "c̄" : "C̈";
            return {"Ć " + left + ' ' + symbol + ' ' + right + " ć", operation == 0 ? a + b : operation == 1 ? a - b : a * b};
        };
        for (int i = 0; i < 200; ++i) { const auto [source, expected] = tree(3); equal(value(source), std::to_string(expected)); }
    });
    suite.test("declaration, mutation and lexical shadowing", [] {
        equal(fc::capture("C ƈC Ç ƇC ċ Ĉ C ƈC Ç ƇCc ċ C̶ Ć ƈC ć ċ ĉ ƈC Ç ƈC C̄ ƇCC ċ C̶ Ć ƈC ć ċ").output, "2\n4\n");
    });
    suite.test("undefined names, redeclaration and readonly builtins", [] {
        for (const std::string source : {"C ƈC Ç ƇC ċ C ƈC Ç ƇCc ċ", "ƈC Ç ƇC ċ", "Ĉ C ƈC Ç ƇC ċ ĉ ƈC ċ", "C̶ Ç ƇC ċ"}) error([&] { (void)fc::capture(source); });
    });
    suite.test("if / else if / else", [] {
        equal(fc::capture("CC Ć c̆ ć Ĉ C̶ Ć ƇC ć ċ ĉ cc CC Ć C̆ ć Ĉ C̶ Ć ƇCc ć ċ ĉ cc Ĉ C̶ Ć ƇCC ć ċ ĉ").output, "2\n");
    });
    suite.test("while, continue and break", [] {
        equal(fc::capture(R"(
C ƈC Ç Ƈc ċ
Cc Ć C̆ ć Ĉ
    ƈC Ç ƈC C̄ ƇC ċ
    CC Ć ƈC ÇÇ ƇCc ć Ĉ Ccc ċ ĉ
    CC Ć ƈC c̲ ƇCcc ć Ĉ CcC ċ ĉ
    C̶ Ć ƈC ć ċ
ĉ
)").output, "1\n3\n4\n");
    });
    suite.test("for / range scope", [] {
        const std::string source = "CCC Ć ƈC CCc C̶CCC Ć ƇCC ć ć Ĉ C̶ Ć ƈC ć ċ ĉ";
        equal(fc::capture(source).output, "0\n1\n2\n"); error([&] { (void)fc::capture(source + " ƈC ċ"); });
    });
    suite.test("Unicode for and snapshot iteration", [] {
        equal(fc::capture("CCC Ć ƈC CCc " + fc::encode_string("я🦊") + " ć Ĉ C̶ Ć ƈC ć ċ ĉ").output, "я\n🦊\n");
        equal(fc::capture("C ƈC Ç Č ƇC Ċ ƇCc č ċ CCC Ć ƈc CCc ƈC ć Ĉ C̶c Ć ƈC Ċ ƇCC ć ċ ĉ C̶ Ć ƈC ć ċ").output, "[1, 2, 3, 3]\n");
    });
    suite.test("nested list mutation and aliasing", [] {
        equal(fc::capture("C ƈC Ç Č Č ƇC č č ċ C ƈc Ç ƈC ċ ƈc Č Ƈc č Č Ƈc č Ç ƇCC ċ C̶ Ć ƈC ć ċ").output, "[[3]]\n");
        error([] { (void)fc::capture("C ƈC Ç " + fc::encode_string("a") + " ċ ƈC Č Ƈc č Ç " + fc::encode_string("b") + " ċ"); });
    });
    const std::vector<std::pair<std::string, std::string>> example_outputs{
        {"hello.fc", "Привет, C!\n"}, {"factorial.fc", "720\n"}, {"fibonacci.fc", "6765\n"},
        {"map.fc", "[1, 4, 9, 16]\n"}, {"closure.fc", "1 2 3\n"},
        {"lists.fc", "[10, 2, 3, 4]\n4\n3\n3\n"}, {"loops.fc", "31\n"}, {"echo.fc", "Привет 🦊\n"}
    };
    for (const auto& example : example_outputs) {
        const auto& name = example.first;
        const auto& expected = example.second;
        suite.test("unchanged source example " + name, [&] { equal(fc::capture(read(examples / name), "Привет 🦊\n").output, expected); });
    }
    suite.test("anonymous functions and immediate invocation", [] { equal(value("Ć c Ć ƈC ć Ĉ cC ƈC C̈ ƈC ċ ĉ ć Ć ƇCC ć"), "9"); });
    suite.test("function aliases", [] { equal(fc::capture("c ƈC Ć ƈc ć Ĉ cC ƈc C̄ ƇC ċ ĉ C ƈCC Ç ƈC ċ ƈCC Ć ƇCc ć ċ").value, "3"); });
    suite.test("closures use definition scope, not caller scope", [] {
        equal(fc::capture("C ƈC Ç ƇC ċ c ƈc Ć ć Ĉ cC ƈC ċ ĉ c ƈCC Ć ƈC ć Ĉ cC ƈc Ć ć ċ ĉ ƈCC Ć ƇCCC ć ċ").value, "1");
    });
    suite.test("iteration closures have distinct bindings", [] {
        equal(fc::capture(R"(
C ƈC Ç Č č ċ
CCC Ć ƈc CCc C̶CCC Ć ƇCC ć ć Ĉ
    C̶c Ć ƈC Ċ c Ć ć Ĉ cC ƈc ċ ĉ ć ċ
ĉ
C̶ Ć ƈC Č Ƈc č Ć ć Ċ ƈC Č ƇC č Ć ć Ċ ƈC Č ƇCc č Ć ć ć ċ
)").output, "0 1 2\n");
    });
    suite.test("mutual recursion", [] {
        equal(fc::capture(R"(
c ƈC Ć ƈCC ć Ĉ
    CC Ć ƈCC ÇÇ Ƈc ć Ĉ cC C̆ ċ ĉ
    cC ƈc Ć ƈCC c̄ ƇC ć ċ
ĉ
c ƈc Ć ƈCC ć Ĉ
    CC Ć ƈCC ÇÇ Ƈc ć Ĉ cC c̆ ċ ĉ
    cC ƈC Ć ƈCC c̄ ƇC ć ċ
ĉ
ƈC Ć ƇCCc ć ċ
)").value, "true");
    });
    suite.test("return escapes nested loops", [] { equal(fc::capture("c ƈC Ć ć Ĉ Cc Ć C̆ ć Ĉ CCC Ć ƈc CCc Č ƇCC č ć Ĉ cC ƈc ċ ĉ ĉ ĉ ƈC Ć ć ċ").value, "3"); });
    suite.test("implicit and empty return", [] {
        equal(fc::capture("c ƈC Ć ć Ĉ ĉ ƈC Ć ć ċ").value, "null"); equal(fc::capture("c ƈC Ć ć Ĉ cC ċ ĉ ƈC Ć ć ċ").value, "null");
    });
    suite.test("arity and noncallable errors", [] {
        for (const std::string source : {"ƇC Ć ć ċ", "c ƈC Ć ƈc ć Ĉ ĉ ƈC Ć ć ċ", "c ƈC Ć ć Ĉ ĉ ƈC Ć ƇC ć ċ"}) error([&] { (void)fc::capture(source); });
    });
    suite.test("language call stack", [] {
        try { (void)fc::capture("c ƈC Ć ć Ĉ cC ƇC c̈ Ƈc ċ ĉ ƈC Ć ć ċ"); }
        catch (const fc::Error& caught) { require(caught.diagnostic().find("вызов ƈC") != std::string::npos); return; }
        throw std::runtime_error("Ожидалась ошибка");
    });
    suite.test("line input, EOF and Unicode", [&] {
        equal(fc::capture(read(examples / "echo.fc"), "Привет!\n\n🦊\r\n").output, "Привет!\n\n🦊\n");
        equal(fc::capture("C̶ Ć c̶ Ć " + fc::encode_string("> ") + " ć ć ċ", "hello\n").output, "> hello\n");
        equal(value("c̶ Ć ć"), "null");
    });
    suite.test("print formatting and null return", [] {
        const auto result = fc::capture("C̶ Ć C̆ Ċ c̆ Ċ C̊ Ċ Č " + fc::encode_string("я") + " č ć ċ");
        equal(result.output, "true false null [\"я\"]\n"); equal(result.value, "null");
    });
    suite.test("list builtins and str", [] {
        equal(fc::capture("C ƈC Ç Č ƇC č ċ C̶c Ć ƈC Ċ ƇCc ć ċ C̶ Ć C̶C Ć ƈC ć Ċ C̶CC Ć ƈC Ċ Ƈc ć Ċ ƈC ć ċ").output, "2 1 [2]\n");
        equal(value("C̶Cc Ć C̆ ć"), "true");
    });
    suite.test("numeric conversions", [] {
        equal(value("C̶cC Ć " + fc::encode_string(" -42 ") + " ć"), "-42");
        equal(value("C̶cC Ć " + fc::encode_float(-2.8) + " ć"), "-2");
        equal(value("C̶cc Ć " + fc::encode_string("2.5") + " ć"), "2.5");
        for (const std::string text : {"nan", "inf", "not-a-number"}) error([&] { (void)value("C̶cc Ć " + fc::encode_string(text) + " ć"); });
    });
    suite.test("range and assert", [] {
        equal(value("C̶CCC Ć ƇCc ć"), "[0, 1]");
        equal(value("C̶CCC Ć ƇC Ċ ƇCcC Ċ ƇCc ć"), "[1, 3]");
        equal(value("C̶CCC Ć ƇCC Ċ Ƈc Ċ c̄ ƇC ć"), "[3, 2, 1]");
        equal(value("C̶CcC Ć C̆ ć"), "null"); error([] { (void)value("C̶CcC Ć c̆ ć"); });
    });
    suite.test("native argument validation", [] {
        for (const std::string source : {"C̶C Ć ć", "C̶C Ć ƇC ć", "C̶c Ć Ȼ Ċ ƇC ć", "C̶CC Ć Č č ć", "C̶CCC Ć Ƈc Ċ ƇC Ċ Ƈc ć", "C̶CCC Ć C̆ ć", "c̶ Ć ƇC ć", "C̶cC Ć C̊ ć", "C̶cc Ć Č č ć"}) error([&] { (void)value(source); });
    });
    suite.test("cyclic lists format and compare", [] {
        equal(fc::capture("C ƈC Ç Č č ċ C̶c Ć ƈC Ċ ƈC ć ċ C ƈc Ç Č č ċ C̶c Ć ƈc Ċ ƈc ć ċ C̶ Ć ƈC Ċ ƈC ÇÇ ƈc ć ċ").output, "[[...]] true\n");
    });
    suite.test("exact step limit and boundary completion", [] {
        std::istringstream input; std::ostringstream output; fc::Runtime runtime(input, output, {40, 128, 1000});
        error([&] { runtime.run(fc::parse("Cc Ć C̆ ć Ĉ ĉ")); }, fc::Category::limit);
        require(runtime.steps() == 40); equal(fc::capture("ƇC ċ", "", {2, 128, 1000}).value, "1");
    });
    suite.test("recursion limits unwind correctly", [] {
        std::istringstream input; std::ostringstream output; fc::Runtime runtime(input, output, {10000, 10, 1000});
        error([&] { runtime.run(fc::parse("c ƈC Ć ć Ĉ cC ƈC Ć ć ċ ĉ ƈC Ć ć ċ")); }, fc::Category::limit);
        runtime.reset_budget(); runtime.run(fc::parse("ƇC C̄ ƇCc ċ")); equal(runtime.last_value_text(), "3");
    });
    suite.test("output limits count UTF-8 bytes", [] {
        const auto source = "C̶ Ć " + fc::encode_string("я") + " ć ċ";
        equal(fc::capture(source, "", {1000, 128, 3}).output, "я\n");
        error([&] { (void)fc::capture(source, "", {1000, 128, 2}); }, fc::Category::limit);
    });
    suite.test("large values bounded before allocation", [] {
        for (const auto& source : std::vector<std::string>{"Č ƇC č C̈ " + number(100001), fc::encode_string("x") + " C̈ " + number(1000001), "C̶CCC Ć " + number(100001) + " ć", "ƇCc C̈C " + number(1000000)})
            error([&] { (void)value(source); }, fc::Category::limit);
        const auto huge = fc::encode_int("1267650600228229401496703205376");
        equal(value("Ȼ C̈ " + huge), ""); equal(value("Č č C̈ " + huge), "[]");
    });
    suite.test("shared graph output is bounded while formatting", [] {
        const std::string source = "C ƈC Ç Č ƇC č ċ CCC Ć ƈc CCc C̶CCC Ć " + number(30) + " ć ć Ĉ ƈC Ç Č ƈC Ċ ƈC č ċ ĉ C̶ Ć ƈC ć ċ";
        error([&] { (void)fc::capture(source, "", {10000, 128, 128}); }, fc::Category::limit);
    });
    suite.test("unreachable cycles reclaimed between commands", [] {
        std::istringstream input; std::ostringstream output; fc::Runtime runtime(input, output);
        runtime.run(fc::parse("c ƈC Ć ć Ĉ C ƈc Ç Č č ċ C̶c Ć ƈc Ċ ƈc ć ċ cC ċ ĉ"));
        const auto baseline = runtime.live_objects();
        for (int i = 0; i < 300; ++i) { runtime.reset_budget(); runtime.run(fc::parse("ƈC Ć ć ċ")); }
        require(runtime.live_objects() == baseline, "Мёртвые циклы остались в памяти");
    });
    suite.test("closures and AST survive between commands", [&] {
        std::istringstream input; std::ostringstream output; fc::Runtime runtime(input, output);
        runtime.run(fc::parse(read(examples / "closure.fc")));
        runtime.reset_budget(); runtime.run(fc::parse("ƈCc Ć ć ċ")); equal(runtime.last_value_text(), "4");
    });
    suite.test("invalid runtime limits", [] {
        for (const fc::Limits limits : {fc::Limits{0, 128, 100}, fc::Limits{10, 0, 100}, fc::Limits{10, 128, 0}}) {
            bool caught = false;
            try { std::istringstream input; std::ostringstream output; fc::Runtime runtime(input, output, limits); }
            catch (const std::invalid_argument&) { caught = true; }
            require(caught);
        }
    });

    suite.test("CLI help, version and syntax", [] {
        require(cli({}).code == 0); require(cli({"--help"}).output.find("C++20") != std::string::npos);
        equal(cli({"--version"}).output, "Fucking C " + std::string(fc::version) + " (C++20)\n"); require(cli({"syntax"}).output.find("Ƈ") != std::string::npos);
    });
    suite.test("CLI native run and shortcut", [&] {
        const auto path = (examples / "factorial.fc").string();
        for (const auto& args : std::vector<std::vector<std::string>>{{"run", path}, {path}}) {
            const auto result = cli(args); require(result.code == 0, result.errors); equal(result.output, "720\n"); equal(result.errors, "");
        }
    });
    suite.test("CLI input and stdin", [&] {
        const auto path = (examples / "echo.fc").string();
        equal(cli({path}, "Привет 🦊\n").output, "Привет 🦊\n"); equal(cli({path, "--input", "Привет 🦊\n"}, "ignored").output, "Привет 🦊\n");
    });
    suite.test("CLI check never executes", [&] {
        const auto path = temporary.file("infinite.fc", "Cc Ć C̆ ć Ĉ ĉ");
        const auto result = cli({"check", path}); require(result.code == 0); require(result.output.find("OK:") != std::string::npos);
    });
    suite.test("CLI token inspection", [&] {
        const auto path = temporary.file("tokens.fc", "C ƈC Ç ƇCC ċ");
        const auto result = cli({"tokens", path}); require(result.code == 0); require(result.output.find("1:1\tLet") != std::string::npos);
    });
    suite.test("CLI literals emit executable source", [] {
        const auto integer = cli({"literal", "int", "-42"}); require(integer.code == 0); equal(value(integer.output), "-42");
        const auto real = cli({"literal", "float", "0.1"}); require(real.code == 0); equal(value(real.output), "0.1");
        const auto text = cli({"literal", "str", "Привет!"}); require(text.code == 0); equal(value(text.output), "Привет!");
        require(cli({"literal", "float", "inf"}).code == 2);
    });
    suite.test("CLI stats stay out of stdout", [&] {
        const auto result = cli({(examples / "factorial.fc").string(), "--stats"});
        require(result.code == 0); equal(result.output, "720\n"); require(result.errors.find("Шагов:") != std::string::npos);
    });
    suite.test("CLI syntax exit 2 and runtime exit 1", [&] {
        const auto invalid = temporary.file("syntax.fc", "C ƈC Ç ƇC ċ\n  ???");
        const auto result = cli({invalid}); require(result.code == 2); require(result.errors.find(":2:3:") != std::string::npos);
        const auto failure = temporary.file("runtime.fc", "c ƈC Ć ć Ĉ cC ƇC c̈ Ƈc ċ ĉ ƈC Ć ć ċ");
        const auto runtime = cli({failure}); require(runtime.code == 1); require(runtime.errors.find("вызов ƈC") != std::string::npos);
    });
    suite.test("CLI limits preserve earlier output", [&] {
        const auto path = temporary.file("limit.fc", "C̶ Ć ƇC ć ċ Cc Ć C̆ ć Ĉ ĉ");
        const auto result = cli({path, "--max-steps", "50", "--stats"});
        require(result.code == 1); equal(result.output, "1\n"); require(result.errors.find("50") != std::string::npos);
    });
    suite.test("CLI file errors and BOM", [&] {
        require(cli({(temporary.path / "missing.fc").string()}).code == 1);
        const auto bom = temporary.file("bom.fc", "﻿C̶ Ć ƇC ć ċ"); equal(cli({bom}).output, "1\n");
        const auto invalid = temporary.file("utf8.fc", "\xff"); require(cli({invalid}).code == 2);
    });
    suite.test("CLI argument validation", [&] {
        const auto path = (examples / "factorial.fc").string();
        const std::vector<std::vector<std::string>> cases{{"run"}, {"check"}, {"literal", "int"}, {path, "--max-steps", "0"}, {path, "--unknown"}, {path, "--input"}, {path, "--stats", "--stats"}};
        for (const auto& args : cases) require(cli(args).code == 2);
    });
    suite.test("REPL state and multiline functions", [] {
        const auto result = cli({"repl"}, "C ƈC Ç ƇCc ċ\nƈC C̄ ƇCC\nc ƈc Ć ƈCC ć Ĉ\ncC ƈCC C̈ ƈCC ċ\nĉ\nƈc Ć ƈC ć\n.quit\n");
        require(result.code == 0, result.errors); equal(result.output, "5\n4\n");
    });
    suite.test("REPL recovers after runtime error", [] {
        const auto result = cli({"repl"}, "ƇC c̈ Ƈc\nƇCc C̄ ƇCC\n.quit\n");
        require(result.code == 0); equal(result.output, "5\n"); require(result.errors.find("Деление на ноль") != std::string::npos);
    });
    suite.test("REPL unfinished block at EOF", [] {
        const auto result = cli({"repl"}, "c ƈC Ć ć Ĉ\n"); require(result.code == 2); require(result.errors.find("Незакрытый блок") != std::string::npos);
    });

    std::cout << "\n" << suite.passed << " passed, " << suite.failed << " failed\n";
    return suite.failed ? 1 : 0;
}
