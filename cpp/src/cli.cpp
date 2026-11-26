// SPDX-License-Identifier: WTFPL
#include "fucking_c.hpp"

#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <unordered_set>

namespace fc {
namespace {

class UsageError : public std::runtime_error { using std::runtime_error::runtime_error; };

void help(std::ostream& output) {
    output << "Fucking C " << version << " — нативный интерпретатор на C++20\n\n"
           << "  fc run программа.fc [--input TEXT] [--stats]\n"
           << "  fc программа.fc                    сокращённый запуск\n"
           << "  fc check программа.fc              проверить синтаксис\n"
           << "  fc tokens программа.fc             показать токены\n"
           << "  fc repl                            интерактивная консоль\n"
           << "  fc syntax                          таблица C-команд\n"
           << "  fc literal int|float|str|name VALUE кодирование литерала\n\n"
           << "Параметры run:\n"
           << "  --input TEXT       входной текст вместо stdin\n"
           << "  --max-steps N      лимит шагов (1000000)\n"
           << "  --max-depth N      лимит вложенных вызовов (128)\n"
           << "  --max-output N     лимит байтов вывода (1048576)\n"
           << "  --stats            статистика в stderr\n\n"
           << "Все токены исходника разделяются пробелами. Файлы — UTF-8.\n"
           << "Python, Node.js и браузер для запуска не нужны.\n";
}
void syntax(std::ostream& output) {
    output << "Fucking C: каждый токен отделяется пробелом.\n\n";
    for (const auto& [symbol, meaning] : syntax_symbols()) output << "  " << symbol << "\t" << meaning << '\n';
    output << "\nƈ + c/C — имя. Ƈ + биты c=0,C=1 — целое число.\n"
           << "Ȼ + байты UTF-8 в c/C — строка. Пустая строка: Ȼ.\n"
           << "Пример: fc literal str 'Привет'\n";
}
std::string read_source(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Не удалось открыть файл: " + path);
    std::string result;
    char buffer[8192];
    while (input.read(buffer, sizeof buffer) || input.gcount()) {
        result.append(buffer, static_cast<std::size_t>(input.gcount()));
        if (result.size() > max_source_chars * 4 + 4) throw Error("Файл исходника слишком большой.", Category::limit);
    }
    if (input.bad()) throw std::runtime_error("Ошибка чтения файла: " + path);
    return result;
}
std::uint64_t positive_number(const std::string& text) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() || !value)
        throw UsageError("Лимит должен быть положительным целым числом.");
    return value;
}
double float_argument(std::string_view text) {
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    double value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value))
        throw UsageError("Ожидается конечный float.");
    return value;
}

int repl(std::istream& input, std::ostream& output, std::ostream& errors, bool interactive) {
    Runtime runtime(input, output);
    if (interactive) output << "Fucking C " << version << " / C++20. .help — справка, .quit — выход.\n";
    std::string source, line;
    std::size_t number = 1;
    while (true) {
        if (interactive) { output << (source.empty() ? "C> " : "C… "); output.flush(); }
        if (!std::getline(input, line)) {
            if (!source.empty()) {
                try { (void)parse(source, "<repl:" + std::to_string(number) + ">"); }
                catch (const Error& error) { errors << error.diagnostic() << '\n'; return 2; }
            }
            return 0;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (source.empty() && (line == ".quit" || line == ".exit")) return 0;
        if (source.empty() && line == ".help") { syntax(output); continue; }
        if (source.empty() && line.empty()) continue;
        if (!source.empty()) source += '\n';
        source += line;
        const auto name = "<repl:" + std::to_string(number) + ">";
        try {
            const auto tokens = scan(source, name);
            int balance = 0;
            for (const auto& token : tokens) {
                if (token.kind == "LParen" || token.kind == "LBracket" || token.kind == "LBrace") ++balance;
                else if (token.kind == "RParen" || token.kind == "RBracket" || token.kind == "RBrace") --balance;
            }
            if (balance > 0) continue;
            if (tokens.size() > 1 && tokens[tokens.size() - 2].kind != "Semi" && tokens[tokens.size() - 2].kind != "RBrace") source += " ċ";
            const auto program = parse(source, name);
            runtime.reset_budget(); runtime.run(program);
            if (program.ends_with_expression() && !runtime.last_value_is_null()) runtime.echo_last();
        } catch (const Error& error) { errors << error.diagnostic() << '\n'; }
        source.clear(); ++number;
    }
}

} // namespace

int cli_main(std::vector<std::string> arguments, std::istream& input, std::ostream& output, std::ostream& errors, bool interactive) {
    try {
        if (arguments.empty() || (arguments.size() == 1 && (arguments[0] == "--help" || arguments[0] == "-h"))) { help(output); return 0; }
        if (arguments.size() == 1 && arguments[0] == "--version") { output << "Fucking C " << version << " (C++20)\n"; return 0; }
        const std::unordered_set<std::string> commands{"run", "check", "tokens", "repl", "syntax", "literal", "lit"};
        if (!commands.contains(arguments[0])) {
            if (arguments[0].starts_with('-')) throw UsageError("Неизвестный параметр: " + arguments[0]);
            arguments.insert(arguments.begin(), "run");
        }
        const auto command = arguments[0];
        if (command == "syntax" || command == "repl") {
            if (arguments.size() != 1) throw UsageError("Лишние аргументы команды " + command);
            if (command == "syntax") { syntax(output); return 0; }
            return repl(input, output, errors, interactive);
        }
        if (command == "literal" || command == "lit") {
            if (arguments.size() != 3) throw UsageError("Формат: fc literal int|float|str|name VALUE");
            const auto& kind = arguments[1]; const auto& value = arguments[2];
            if (kind == "int") output << encode_int(value);
            else if (kind == "float") output << encode_float(float_argument(value));
            else if (kind == "str") output << encode_string(value);
            else if (kind == "name") output << encode_name(value);
            else throw UsageError("Неизвестный вид литерала: " + kind);
            output << '\n'; return 0;
        }
        if (command == "check" || command == "tokens") {
            if (arguments.size() != 2) throw UsageError("Укажи ровно один файл .fc.");
            const auto source = read_source(arguments[1]);
            if (command == "check") { (void)parse(source, arguments[1]); output << "OK: " << arguments[1] << " — синтаксис корректен.\n"; }
            else for (const auto& token : scan(source, arguments[1])) {
                std::string raw = token.raw;
                if (raw.size() > 100) raw = raw.substr(0, 100) + "…";
                output << token.line << ':' << token.column << '\t' << token.kind << '\t' << raw << '\n';
            }
            return 0;
        }
        std::string path, input_text;
        bool explicit_input = false, stats = false, positional_only = false;
        Limits limits;
        std::unordered_set<std::string> seen;
        for (std::size_t i = 1; i < arguments.size(); ++i) {
            const auto& arg = arguments[i];
            if (!positional_only && arg == "--") { positional_only = true; continue; }
            if (!positional_only && arg.starts_with('-')) {
                if (!seen.insert(arg).second) throw UsageError("Параметр указан повторно: " + arg);
                if (arg == "--stats") { stats = true; continue; }
                if (arg == "--help" || arg == "-h") { help(output); return 0; }
                if (arg != "--input" && arg != "--max-steps" && arg != "--max-depth" && arg != "--max-output") throw UsageError("Неизвестный параметр: " + arg);
                if (i + 1 == arguments.size()) throw UsageError("После " + arg + " требуется значение.");
                const auto& value = arguments[++i];
                if (arg == "--input") { explicit_input = true; input_text = value; }
                else {
                    const auto number = positive_number(value);
                    if (arg == "--max-steps") limits.steps = number;
                    else {
                        if (number > std::numeric_limits<std::size_t>::max()) throw UsageError("Лимит слишком большой для этой платформы.");
                        if (arg == "--max-depth") limits.call_depth = static_cast<std::size_t>(number);
                        else limits.output_bytes = static_cast<std::size_t>(number);
                    }
                }
            } else if (path.empty()) path = arg;
            else throw UsageError("Лишний аргумент: " + arg);
        }
        if (path.empty()) throw UsageError("Укажи файл программы .fc.");
        const auto program = parse(read_source(path), path);
        std::istringstream replacement(input_text);
        Runtime runtime(explicit_input ? static_cast<std::istream&>(replacement) : input, output, limits);
        const auto started = std::chrono::steady_clock::now();
        const auto report = [&] {
            if (stats) {
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                errors << "Шагов: " << runtime.steps() << "; вывод: " << runtime.output_bytes() << " байт; время: " << std::fixed << std::setprecision(2) << ms << " мс.\n";
            }
        };
        try { runtime.run(program); }
        catch (...) { report(); throw; }
        report(); return 0;
    } catch (const UsageError& error) { errors << "Fucking C: " << error.what() << '\n'; return 2; }
    catch (const Error& error) { errors << error.diagnostic() << '\n'; return error.category() == Category::syntax ? 2 : 1; }
    catch (const std::bad_alloc&) { errors << "Fucking C: недостаточно памяти.\n"; return 1; }
    catch (const std::exception& error) { errors << "Fucking C: " << error.what() << '\n'; return 1; }
}

} // namespace fc
