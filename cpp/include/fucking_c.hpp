// SPDX-License-Identifier: WTFPL
#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fc {

inline constexpr std::string_view version = "2.1.0";
inline constexpr std::size_t max_source_chars = 1'000'000;
inline constexpr std::size_t max_integer_bits = 4096;
inline constexpr std::size_t max_string_chars = 1'000'000;
inline constexpr std::size_t max_list_items = 100'000;

enum class Category { syntax, runtime, limit };

struct Site {
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string source_line;
    std::size_t caret_column = 0;
};

class Error : public std::runtime_error {
public:
    Error(std::string message, Category category = Category::syntax, Site site = {});
    Category category() const noexcept;
    const Site& site() const noexcept;
    void add_frame(std::string name, const Site& call);
    std::string diagnostic() const;
private:
    Category category_;
    Site site_;
    std::vector<std::string> trace_;
};

struct TokenInfo {
    std::string kind;
    std::string raw;
    std::size_t line;
    std::size_t column;
};

struct Program {
    struct Data;
    std::shared_ptr<Data> data;
    std::size_t statement_count() const;
    bool ends_with_expression() const;
};

std::vector<TokenInfo> scan(std::string_view source, std::string name = "<code>");
Program parse(std::string_view source, std::string name = "<code>");
std::vector<std::pair<std::string, std::string>> syntax_symbols();
std::string encode_int(std::string_view decimal);
std::string encode_float(double value);
std::string encode_string(std::string_view value);
std::string encode_name(std::string_view value);

struct Limits {
    std::uint64_t steps = 1'000'000;
    std::size_t call_depth = 128;
    std::size_t output_bytes = 1'048'576;
};

class Runtime {
public:
    Runtime(std::istream& input, std::ostream& output, Limits limits = {});
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    void run(const Program& program);
    void reset_budget();
    std::string last_value_text() const;
    std::string last_value_type() const;
    bool last_value_is_null() const;
    void echo_last();
    std::uint64_t steps() const noexcept;
    std::size_t output_bytes() const noexcept;
    std::size_t live_objects() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct Result {
    std::string output;
    std::string value;
    std::string type;
    std::uint64_t steps;
};

Result capture(std::string_view source, std::string input = "", Limits limits = {});
int cli_main(std::vector<std::string> arguments, std::istream& input, std::ostream& output,
             std::ostream& errors, bool interactive = false);

} // namespace fc
