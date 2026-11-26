// SPDX-License-Identifier: WTFPL
#include "internal.hpp"

#include <bit>
#include <charconv>
#include <sstream>

namespace fc {

Error::Error(std::string message, Category category, Site location)
    : std::runtime_error(std::move(message)), category_(category), site_(std::move(location)) {}
Category Error::category() const noexcept { return category_; }
const Site& Error::site() const noexcept { return site_; }
void Error::add_frame(std::string name, const Site& call) {
    if (trace_.size() < 12)
        trace_.push_back("  вызов " + name + ": " + call.file + ":" + std::to_string(call.line) + ":" + std::to_string(call.column));
}
std::string Error::diagnostic() const {
    const std::string label = category_ == Category::syntax ? "ошибка синтаксиса" : category_ == Category::limit ? "лимит" : "ошибка выполнения";
    std::ostringstream out;
    if (site_.file.empty()) out << "Fucking C: ";
    else out << site_.file << ':' << site_.line << ':' << site_.column << ": ";
    out << label << ": " << what();
    if (!site_.source_line.empty()) out << "\n  " << site_.source_line << "\n  " << std::string(site_.caret_column, ' ') << '^';
    for (const auto& frame : trace_) out << '\n' << frame;
    return out.str();
}

} // namespace fc

namespace fc::detail {

std::u32string decode_utf8(std::string_view text) {
    std::u32string result;
    result.reserve(std::min(text.size(), max_source_chars));
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        const auto first = static_cast<unsigned char>(text[i++]);
        char32_t value = 0;
        unsigned count = 0;
        if (first < 0x80) value = first;
        else if (first >= 0xc2 && first <= 0xdf) { value = first & 0x1f; count = 1; }
        else if (first >= 0xe0 && first <= 0xef) { value = first & 0x0f; count = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { value = first & 0x07; count = 3; }
        else throw Error("Некорректный UTF-8, байт " + std::to_string(start + 1) + '.');
        if (i + count > text.size()) throw Error("Незавершённая последовательность UTF-8.");
        for (unsigned j = 0; j < count; ++j) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) throw Error("Некорректный продолжающий байт UTF-8.");
            value = (value << 6) | (byte & 0x3f);
        }
        if ((count == 1 && value < 0x80) || (count == 2 && value < 0x800) ||
            (count == 3 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            throw Error("Неканоническая или недопустимая последовательность UTF-8.");
        result.push_back(value);
        if (result.size() > max_source_chars) throw Error("Текст длиннее 1 000 000 символов.", Category::limit);
    }
    return result;
}

std::string encode_utf8(std::u32string_view text) {
    std::string result;
    for (char32_t value : text) {
        if (value <= 0x7f) result.push_back(static_cast<char>(value));
        else if (value <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (value >> 6)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else if (value <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (value >> 12)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (value >> 18)));
            result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
    }
    return result;
}

bool whitespace(char32_t c) {
    return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}

static std::string normalize(std::u32string_view raw) {
    std::u32string input(raw), result;
    for (auto& c : input) {
        if (c == U'С') c = U'C';
        if (c == U'с') c = U'c';
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        char32_t c = input[i];
        if ((c == U'C' || c == U'c') && i + 1 < input.size()) {
            char32_t upper = 0;
            switch (input[i + 1]) {
                case 0x301: upper = U'Ć'; break;
                case 0x302: upper = U'Ĉ'; break;
                case 0x307: upper = U'Ċ'; break;
                case 0x30c: upper = U'Č'; break;
                case 0x327: upper = U'Ç'; break;
                default: break;
            }
            if (upper) {
                c = c == U'C' ? upper : upper == U'Ç' ? U'ç' : upper + 1;
                ++i;
            }
        }
        result.push_back(c);
    }
    return encode_utf8(result);
}

const std::vector<Symbol>& symbols() {
    static const std::vector<Symbol> table = {
        {"C", K::Let, "let"}, {"c", K::Fn, "fn"}, {"CC", K::If, "if"}, {"cc", K::Else, "else"},
        {"Cc", K::While, "while"}, {"cC", K::Return, "return"}, {"CCC", K::For, "for"}, {"CCc", K::In, "in"},
        {"CcC", K::Break, "break"}, {"Ccc", K::Continue, "continue"}, {"cCC", K::And, "and"}, {"cCc", K::Or, "or"}, {"ccC", K::Not, "not"},
        {"Ć", K::LParen, "("}, {"ć", K::RParen, ")"}, {"Ĉ", K::LBrace, "{"}, {"ĉ", K::RBrace, "}"},
        {"Č", K::LBracket, "["}, {"č", K::RBracket, "]"}, {"Ċ", K::Comma, ","}, {"ċ", K::Semi, ";"},
        {"Ç", K::Assign, "="}, {"ÇÇ", K::Eq, "=="}, {"çç", K::Ne, "!="}, {"C̄", K::Plus, "+"}, {"c̄", K::Minus, "-"},
        {"C̈", K::Mul, "*"}, {"c̈", K::Div, "/"}, {"c̃", K::FloorDiv, "//"}, {"C̃", K::Mod, "%"}, {"C̈C", K::Pow, "**"},
        {"C̲", K::Lt, "<"}, {"C̲C", K::Le, "<="}, {"c̲", K::Gt, ">"}, {"c̲c", K::Ge, ">="},
        {"C̆", K::True, "true"}, {"c̆", K::False, "false"}, {"C̊", K::NullValue, "null"},
        {"C̶", K::Builtin, "print"}, {"c̶", K::Builtin, "input"}, {"C̶C", K::Builtin, "len"}, {"C̶c", K::Builtin, "append"},
        {"C̶CC", K::Builtin, "pop"}, {"C̶Cc", K::Builtin, "str"}, {"C̶cC", K::Builtin, "int"}, {"C̶cc", K::Builtin, "float"},
        {"C̶CCC", K::Builtin, "range"}, {"C̶CCc", K::Builtin, "type"}, {"C̶CcC", K::Builtin, "assert"}
    };
    return table;
}

std::string kind_name(K kind) {
    switch (kind) {
#define FC_KIND(name) case K::name: return #name;
        FC_KIND(End) FC_KIND(Let) FC_KIND(Fn) FC_KIND(If) FC_KIND(Else) FC_KIND(While) FC_KIND(Return)
        FC_KIND(For) FC_KIND(In) FC_KIND(Break) FC_KIND(Continue) FC_KIND(And) FC_KIND(Or) FC_KIND(Not)
        FC_KIND(LParen) FC_KIND(RParen) FC_KIND(LBrace) FC_KIND(RBrace) FC_KIND(LBracket) FC_KIND(RBracket)
        FC_KIND(Comma) FC_KIND(Semi) FC_KIND(Assign) FC_KIND(Eq) FC_KIND(Ne) FC_KIND(Plus) FC_KIND(Minus)
        FC_KIND(Mul) FC_KIND(Div) FC_KIND(FloorDiv) FC_KIND(Mod) FC_KIND(Pow) FC_KIND(Lt) FC_KIND(Le) FC_KIND(Gt) FC_KIND(Ge)
        FC_KIND(True) FC_KIND(False) FC_KIND(NullValue) FC_KIND(Ident) FC_KIND(Number) FC_KIND(String) FC_KIND(Builtin)
#undef FC_KIND
    }
    return "Unknown";
}

Site site(const Token& token) {
    if (!token.source) return {};
    Site location{token.source->name, token.line, token.column, "", 0};
    const auto& chars = token.source->chars;
    const auto start = token.source->line_starts.at(token.line - 1);
    std::size_t end = start;
    while (end < chars.size() && chars[end] != U'\r' && chars[end] != U'\n' && chars[end] != 0x2028 && chars[end] != 0x2029) ++end;
    const std::size_t left = token.begin > start + 100 ? token.begin - 100 : start;
    const std::size_t right = std::min(end, left + 180);
    if (left > start) { location.source_line = "…"; location.caret_column = 1; }
    std::size_t width = left > start ? 1 : 0;
    for (std::size_t i = left; i < right; ++i) {
        if (i == token.begin) location.caret_column = width;
        const char32_t c = chars[i];
        if (c == U'\t') {
            const auto count = 4 - width % 4;
            location.source_line.append(count, ' ');
            width += count;
        } else if (c < 32 || c == 127) {
            location.source_line += '?'; ++width;
        } else {
            location.source_line += encode_utf8(std::u32string_view(&c, 1));
            if (!(c >= 0x300 && c <= 0x36f)) ++width;
        }
    }
    if (token.begin >= right) location.caret_column = width;
    if (right < end) location.source_line += "…";
    return location;
}

[[noreturn]] void fail(std::string message, const Token& token, Category category) {
    throw Error(std::move(message), category, site(token));
}

std::size_t integer_bits(const Integer& value) {
    if (value == 0) return 0;
    Integer magnitude = value < 0 ? -value : value;
    return boost::multiprecision::msb(magnitude) + 1;
}

Integer decimal_integer(std::string_view text, const Token& token, Category category) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    if (text.size() > 1400) fail("Слишком длинное целое число.", token, Category::limit);
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) { negative = text.front() == '-'; text.remove_prefix(1); }
    if (text.empty()) fail("Ожидается десятичная запись целого числа.", token, category);
    Integer value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') fail("Ожидается десятичная запись целого числа.", token, category);
        value *= 10; value += c - '0';
    }
    if (integer_bits(value) > max_integer_bits) fail("Целое число превышает 4096 бит.", token, Category::limit);
    return negative ? Integer(-value) : value;
}

std::string integer_literal(const Integer& value) {
    if (integer_bits(value) > max_integer_bits) throw Error("Целое число превышает 4096 бит.", Category::limit);
    Integer magnitude = value < 0 ? -value : value;
    std::string result = value < 0 ? "c̄ Ƈ" : "Ƈ";
    const auto count = integer_bits(magnitude);
    if (!count) result += 'c';
    for (std::size_t i = count; i > 0; --i) result += boost::multiprecision::bit_test(magnitude, static_cast<unsigned>(i - 1)) ? 'C' : 'c';
    return result;
}

std::pair<Integer, Integer> double_ratio(double value) {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
    if (!std::isfinite(value)) throw Error("Float должен быть конечным.");
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const auto exponent = static_cast<int>((bits >> 52) & 0x7ff);
    std::uint64_t mantissa = bits & ((std::uint64_t{1} << 52) - 1);
    int shift = -1074;
    if (exponent) { mantissa |= std::uint64_t{1} << 52; shift = exponent - 1023 - 52; }
    Integer numerator = mantissa, denominator = 1;
    if (shift >= 0) numerator <<= shift;
    else denominator <<= -shift;
    if (numerator == 0) denominator = 1;
    else while ((numerator & 1) == 0 && (denominator & 1) == 0) { numerator >>= 1; denominator >>= 1; }
    if (std::signbit(value)) numerator = -numerator;
    return {numerator, denominator};
}

double divide_integers(const Integer& a, const Integer& b, const Token& token) {
    if (b == 0) fail("Деление на ноль.", token);
    const bool negative = (a < 0) != (b < 0);
    Integer numerator = a < 0 ? -a : a, denominator = b < 0 ? -b : b;
    if (numerator == 0) return negative ? -0.0 : 0.0;
    int exponent = static_cast<int>(integer_bits(numerator)) - static_cast<int>(integer_bits(denominator));
    if (exponent >= 0) { if (numerator < (denominator << exponent)) --exponent; }
    else if ((numerator << -exponent) < denominator) --exponent;
    if (exponent > 1023) fail("Результат не помещается в float.", token, Category::limit);
    const int scale = exponent >= -1022 ? 52 - exponent : 1074;
    if (scale >= 0) numerator <<= scale;
    else denominator <<= -scale;
    Integer quotient = numerator / denominator;
    const Integer remainder = numerator % denominator;
    if (remainder * 2 > denominator || (remainder * 2 == denominator && (quotient & 1) != 0)) ++quotient;
    const double result = std::ldexp(quotient.convert_to<double>(), -scale);
    if (!std::isfinite(result)) fail("Результат не помещается в float.", token, Category::limit);
    return negative ? -result : result;
}

double to_double(const Integer& value, const Token& token) { return divide_integers(value, Integer(1), token); }
std::string float_text(double value) {
    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::general);
    if (converted.ec != std::errc{}) throw Error("Не удалось представить float.", Category::runtime);
    std::string result(buffer, converted.ptr);
    if (result.find_first_of(".eE") == std::string::npos) result += ".0";
    return result;
}

std::vector<Token> lex(std::string_view text, std::string name) {
    auto source = std::make_shared<Source>();
    source->name = std::move(name);
    try { source->chars = decode_utf8(text); }
    catch (const Error& error) { throw Error(error.what(), error.category(), {source->name, 1, 1, "", 0}); }
    source->line_starts.push_back(0);
    const auto& chars = source->chars;
    for (std::size_t i = 0; i < chars.size(); ++i) {
        if (chars[i] == U'\r') {
            if (i + 1 < chars.size() && chars[i + 1] == U'\n') ++i;
            source->line_starts.push_back(i + 1);
        } else if (chars[i] == U'\n' || chars[i] == 0x2028 || chars[i] == 0x2029) source->line_starts.push_back(i + 1);
    }
    static const auto lookup = [] {
        std::unordered_map<std::string, Symbol> result;
        for (const auto& symbol : symbols()) result.emplace(symbol.text, symbol);
        return result;
    }();
    std::vector<Token> result;
    std::size_t line = 1, column = 1, i = 0;
    while (i < chars.size()) {
        if (whitespace(chars[i])) {
            if (chars[i] == U'\r') {
                if (i + 1 < chars.size() && chars[i + 1] == U'\n') ++i;
                ++line; column = 1;
            } else if (chars[i] == U'\n' || chars[i] == 0x2028 || chars[i] == 0x2029) { ++line; column = 1; }
            else ++column;
            ++i;
            continue;
        }
        const auto begin = i;
        while (i < chars.size() && !whitespace(chars[i])) ++i;
        Token token{K::End, normalize(std::u32string_view(chars).substr(begin, i - begin)), Null{}, line, column, begin, i, source};
        column += i - begin;
        if (const auto found = lookup.find(token.text); found != lookup.end()) {
            token.kind = found->second.kind;
            if (token.kind == K::Builtin) token.text = found->second.name;
        } else {
            const auto normalized = decode_utf8(token.text);
            const auto prefix = normalized.front();
            const bool bits = std::all_of(normalized.begin() + 1, normalized.end(), [](char32_t c) { return c == U'c' || c == U'C'; });
            if (prefix == U'ƈ' && bits && normalized.size() > 1) token.kind = K::Ident;
            else if (prefix == U'Ƈ' && bits && normalized.size() > 1) {
                if (normalized.size() - 1 > max_integer_bits) fail("Целочисленный литерал длиннее 4096 бит.", token, Category::limit);
                Integer number = 0;
                for (std::size_t j = 1; j < normalized.size(); ++j) { number <<= 1; if (normalized[j] == U'C') ++number; }
                token.kind = K::Number; token.value = std::move(number);
            } else if (prefix == U'Ȼ' && bits) {
                if ((normalized.size() - 1) % 8) fail("Строковый литерал должен содержать целые 8-битные байты.", token, Category::syntax);
                std::string value;
                for (std::size_t j = 1; j < normalized.size(); j += 8) {
                    unsigned byte = 0;
                    for (unsigned bit = 0; bit < 8; ++bit) byte = (byte << 1) | (normalized[j + bit] == U'C');
                    value.push_back(static_cast<char>(byte));
                }
                try { (void)decode_utf8(value); }
                catch (const Error&) { fail("В строковом литерале некорректный UTF-8.", token, Category::syntax); }
                token.kind = K::String; token.value = std::move(value);
            } else fail("Неизвестный токен. Все токены, включая скобки, разделяются пробелами.", token, Category::syntax);
        }
        result.push_back(std::move(token));
    }
    result.push_back({K::End, "", Null{}, line, column, i, i, source});
    return result;
}

NodePtr node(N kind, const Token& token, std::vector<NodePtr> children) {
    auto result = std::make_shared<Node>();
    result->kind = kind; result->token = token; result->children = std::move(children);
    for (const auto& child : result->children) if (child) result->depth = std::max(result->depth, child->depth + 1);
    if (result->depth > 256) fail("AST глубже 256 уровней.", token, Category::limit);
    return result;
}

static bool comparison(K kind) { return kind == K::Eq || kind == K::Ne || kind == K::Lt || kind == K::Le || kind == K::Gt || kind == K::Ge; }
static int precedence(K kind) {
    if (comparison(kind)) return 40;
    switch (kind) {
        case K::Or: return 10; case K::And: return 20;
        case K::Plus: case K::Minus: return 50;
        case K::Mul: case K::Div: case K::FloorDiv: case K::Mod: return 60;
        case K::Pow: return 80; default: return -1;
    }
}

class Parser {
    std::vector<Token> tokens_;
    std::size_t position_ = 0, depth_ = 0, function_depth_ = 0, loop_depth_ = 0;
    const Token& current() const { return tokens_[position_]; }
    Token advance() { Token token = current(); if (token.kind != K::End) ++position_; return token; }
    bool match(K kind) { if (current().kind != kind) return false; advance(); return true; }
    Token expect(K kind, std::string message) { if (current().kind != kind) fail(std::move(message), current(), Category::syntax); return advance(); }
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    Program parse() {
        auto data = std::make_shared<Program::Data>();
        data->source = tokens_.front().source;
        while (current().kind != K::End) data->statements.push_back(statement());
        return {std::move(data)};
    }
private:
    NodePtr statement() {
        DepthGuard guard(depth_, current());
        const Token token = current();
        if (match(K::Let)) {
            const auto name = expect(K::Ident, "После C ожидается имя вида ƈC.");
            expect(K::Assign, "После имени ожидается Ç.");
            auto value = expression();
            expect(K::Semi, "Объявление заканчивается ċ.");
            auto result = node(N::Let, name, {value}); result->name = name.text; return result;
        }
        if (token.kind == K::Fn && tokens_[position_ + 1].kind == K::Ident) {
            advance(); const auto name = advance();
            auto result = node(N::FunctionDeclaration, name, {function(token)}); result->name = name.text; return result;
        }
        if (match(K::If)) return if_statement(token);
        if (match(K::While)) {
            auto condition_node = condition(); ++loop_depth_;
            auto body = block(); --loop_depth_;
            return node(N::While, token, {condition_node, body});
        }
        if (match(K::For)) {
            expect(K::LParen, "После CCC ожидается Ć.");
            const auto name = expect(K::Ident, "Ожидается имя переменной цикла.");
            expect(K::In, "После переменной цикла ожидается CCc (in).");
            auto iterable = expression(); expect(K::RParen, "Заголовок цикла заканчивается ć.");
            ++loop_depth_; auto body = block(); --loop_depth_;
            auto result = node(N::For, token, {iterable, body}); result->name = name.text; return result;
        }
        if (match(K::Return)) {
            if (!function_depth_) fail("cC (return) допустим только внутри функции.", token, Category::syntax);
            std::vector<NodePtr> children;
            if (current().kind != K::Semi) children.push_back(expression());
            expect(K::Semi, "Возврат заканчивается ċ.");
            return node(N::Return, token, std::move(children));
        }
        if (token.kind == K::Break || token.kind == K::Continue) {
            advance();
            if (!loop_depth_) fail("break/continue допустимы только внутри собственного цикла.", token, Category::syntax);
            expect(K::Semi, "После break/continue ожидается ċ.");
            return node(token.kind == K::Break ? N::Break : N::Continue, token);
        }
        if (token.kind == K::LBrace) return block();
        auto value = expression();
        if (match(K::Assign)) {
            if (value->kind != N::Name && value->kind != N::Index) fail("Присваивать можно имени или элементу списка.", value->token, Category::syntax);
            auto right = expression(); expect(K::Semi, "Присваивание заканчивается ċ.");
            return node(N::Assign, token, {value, right});
        }
        expect(K::Semi, "После выражения ожидается ċ.");
        return node(N::ExpressionStatement, token, {value});
    }
    NodePtr condition() {
        expect(K::LParen, "Перед условием ожидается Ć."); auto value = expression();
        expect(K::RParen, "После условия ожидается ć."); return value;
    }
    NodePtr if_statement(const Token& token) {
        DepthGuard guard(depth_, token);
        auto test = condition(); auto yes = block();
        std::vector<NodePtr> children{test, yes};
        if (match(K::Else)) {
            const auto next = current();
            children.push_back(match(K::If) ? if_statement(next) : block());
        }
        return node(N::If, token, std::move(children));
    }
    NodePtr block() {
        DepthGuard guard(depth_, current());
        const auto token = expect(K::LBrace, "Блок начинается с Ĉ.");
        std::vector<NodePtr> statements;
        while (current().kind != K::RBrace && current().kind != K::End) statements.push_back(statement());
        expect(K::RBrace, "Незакрытый блок: ожидается ĉ.");
        return node(N::Block, token, std::move(statements));
    }
    NodePtr function(const Token& token) {
        expect(K::LParen, "Перед параметрами ожидается Ć.");
        std::vector<Token> parameters;
        if (!match(K::RParen)) {
            do {
                auto parameter = expect(K::Ident, "Параметр должен быть именем вида ƈC.");
                if (std::any_of(parameters.begin(), parameters.end(), [&](const auto& other) { return parameter.text == other.text; }))
                    fail("Параметр указан повторно.", parameter, Category::syntax);
                parameters.push_back(parameter);
                if (parameters.size() > 255) fail("Не больше 255 параметров.", parameter, Category::limit);
                if (match(K::RParen)) break;
                expect(K::Comma, "Параметры разделяются Ċ.");
            } while (!match(K::RParen));
        }
        const auto old_loop = loop_depth_; loop_depth_ = 0; ++function_depth_;
        auto body = block(); --function_depth_; loop_depth_ = old_loop;
        auto result = node(N::Function, token, {body}); result->parameters = std::move(parameters); return result;
    }
    std::vector<NodePtr> sequence(K closing, std::size_t limit) {
        std::vector<NodePtr> result;
        if (match(closing)) return result;
        do {
            result.push_back(expression());
            if (result.size() > limit) fail("Слишком много элементов в конструкции.", current(), Category::limit);
            if (match(closing)) break;
            expect(K::Comma, "Элементы разделяются Ċ; проверь закрывающую скобку.");
        } while (!match(closing));
        return result;
    }
    NodePtr expression(int minimum = 0) {
        DepthGuard guard(depth_, current());
        const auto token = advance();
        NodePtr left;
        switch (token.kind) {
            case K::Number: case K::String: left = node(N::Literal, token); left->value = token.value; break;
            case K::True: case K::False: case K::NullValue:
                left = node(N::Literal, token); if (token.kind != K::NullValue) left->value = token.kind == K::True; break;
            case K::Ident: case K::Builtin: left = node(N::Name, token); left->name = token.text; break;
            case K::Minus: case K::Plus: case K::Not:
                left = node(N::Unary, token, {expression(token.kind == K::Not ? 35 : 70)}); left->op = token.kind; break;
            case K::LParen: left = expression(); expect(K::RParen, "Незакрытая группа: ожидается ć."); break;
            case K::LBracket: left = node(N::List, token, sequence(K::RBracket, max_list_items)); break;
            case K::Fn: left = function(token); break;
            default: fail("Ожидается выражение: число, строка, имя, список или вызов.", token, Category::syntax);
        }
        while (true) {
            const auto op = current();
            if (op.kind == K::LParen && minimum <= 90) {
                advance(); auto arguments = sequence(K::RParen, 255); arguments.insert(arguments.begin(), left);
                left = node(N::Call, op, std::move(arguments)); continue;
            }
            if (op.kind == K::LBracket && minimum <= 90) {
                advance(); auto index = expression(); expect(K::RBracket, "После индекса ожидается č.");
                left = node(N::Index, op, {left, index}); continue;
            }
            const int binding = precedence(op.kind);
            if (binding < minimum) break;
            if (comparison(op.kind)) {
                std::vector<NodePtr> items{left}; std::vector<K> comparisons;
                while (comparison(current().kind)) { comparisons.push_back(advance().kind); items.push_back(expression(41)); }
                left = node(N::Compare, op, std::move(items)); left->comparisons = std::move(comparisons); continue;
            }
            advance(); auto right = expression(op.kind == K::Pow ? binding : binding + 1);
            left = node(N::Binary, op, {left, right}); left->op = op.kind;
        }
        return left;
    }
};

} // namespace fc::detail

namespace fc {
std::size_t Program::statement_count() const { return data ? data->statements.size() : 0; }
bool Program::ends_with_expression() const { return data && !data->statements.empty() && data->statements.back()->kind == detail::N::ExpressionStatement; }
Program parse(std::string_view text, std::string name) { return detail::Parser(detail::lex(text, std::move(name))).parse(); }
std::vector<TokenInfo> scan(std::string_view text, std::string name) {
    std::vector<TokenInfo> result;
    for (const auto& token : detail::lex(text, std::move(name))) {
        auto raw = detail::encode_utf8(std::u32string_view(token.source->chars).substr(token.begin, token.end - token.begin));
        result.push_back({detail::kind_name(token.kind), std::move(raw), token.line, token.column});
    }
    return result;
}
std::vector<std::pair<std::string, std::string>> syntax_symbols() {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& symbol : detail::symbols()) result.emplace_back(symbol.text, symbol.name);
    return result;
}
std::string encode_int(std::string_view decimal) { return detail::integer_literal(detail::decimal_integer(decimal)); }
std::string encode_float(double value) {
    const auto [numerator, denominator] = detail::double_ratio(value);
    auto result = "Ć " + detail::integer_literal(numerator) + " c̈ " + detail::integer_literal(denominator) + " ć";
    if (value == 0 && std::signbit(value)) result = "c̄ " + result;
    return result;
}
std::string encode_string(std::string_view text) {
    (void)detail::decode_utf8(text);
    if (text.size() > (max_source_chars - 1) / 8) throw Error("Строковый литерал не помещается в исходник.", Category::limit);
    std::string result = "Ȼ";
    for (unsigned char byte : text) for (int bit = 7; bit >= 0; --bit) result += ((byte >> bit) & 1) ? 'C' : 'c';
    return result;
}
std::string encode_name(std::string_view text) {
    if (text.empty()) throw Error("Имя не может быть пустым.");
    return "ƈ" + encode_string(text).substr(std::string("Ȼ").size());
}
} // namespace fc
