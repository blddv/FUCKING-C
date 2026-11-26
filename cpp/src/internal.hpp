// SPDX-License-Identifier: WTFPL
#pragma once

#include "fucking_c.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <variant>

namespace fc::detail {

using Integer = boost::multiprecision::cpp_int;
struct Null {};
struct List;
struct Function;
struct Native;
using Value = std::variant<Null, Integer, double, bool, std::string, List*, Function*, Native*>;

enum class K {
    End, Let, Fn, If, Else, While, Return, For, In, Break, Continue, And, Or, Not,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semi, Assign,
    Eq, Ne, Plus, Minus, Mul, Div, FloorDiv, Mod, Pow, Lt, Le, Gt, Ge,
    True, False, NullValue, Ident, Number, String, Builtin
};

struct Source {
    std::string name;
    std::u32string chars;
    std::vector<std::size_t> line_starts;
};

struct Token {
    K kind = K::End;
    std::string text;
    Value value = Null{};
    std::size_t line = 1, column = 1, begin = 0, end = 0;
    std::shared_ptr<Source> source;
};

struct Symbol { std::string text; K kind; std::string name; };
const std::vector<Symbol>& symbols();
std::string kind_name(K kind);
std::u32string decode_utf8(std::string_view text);
std::string encode_utf8(std::u32string_view text);
bool whitespace(char32_t character);
Site site(const Token& token);
[[noreturn]] void fail(std::string message, const Token& token, Category category = Category::runtime);
std::vector<Token> lex(std::string_view text, std::string name);
std::size_t integer_bits(const Integer& value);
Integer decimal_integer(std::string_view text, const Token& token = {}, Category category = Category::syntax);
std::string integer_literal(const Integer& value);
std::pair<Integer, Integer> double_ratio(double value);
double divide_integers(const Integer& numerator, const Integer& denominator, const Token& token);
double to_double(const Integer& value, const Token& token);
std::string float_text(double value);

struct DepthGuard {
    std::size_t& depth;
    explicit DepthGuard(std::size_t& value, const Token& token, std::size_t limit = 512) : depth(value) {
        if (depth >= limit) fail("Слишком глубокая вложенность.", token, Category::limit);
        ++depth;
    }
    ~DepthGuard() { --depth; }
    DepthGuard(const DepthGuard&) = delete;
};

enum class N {
    Literal, Name, Unary, Binary, Compare, Call, Index, List, Function,
    Let, FunctionDeclaration, Assign, ExpressionStatement, Block, If, While, For, Return, Break, Continue
};
struct Node;
using NodePtr = std::shared_ptr<Node>;
struct Node {
    N kind;
    Token token;
    std::vector<NodePtr> children;
    std::vector<Token> parameters;
    std::vector<K> comparisons;
    std::string name;
    K op = K::End;
    Value value = Null{};
    std::size_t depth = 1;
};
NodePtr node(N kind, const Token& token, std::vector<NodePtr> children = {});

} // namespace fc::detail

namespace fc {
struct Program::Data {
    std::vector<detail::NodePtr> statements;
    std::shared_ptr<detail::Source> source;
};
} // namespace fc
