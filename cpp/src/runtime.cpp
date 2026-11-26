// SPDX-License-Identifier: WTFPL
#include "internal.hpp"

#include <charconv>
#include <istream>
#include <ostream>
#include <sstream>
#include <unordered_set>

namespace fc::detail {

struct Binding { Value value; bool mutable_value = true; };
struct Environment {
    Environment* parent = nullptr;
    std::unordered_map<std::string, Binding> bindings;
    bool marked = false;
    void define(const std::string& name, Value value, const Token& token, bool mutable_value = true) {
        if (bindings.contains(name)) fail("Имя " + name + " уже объявлено в этой области.", token);
        bindings.emplace(name, Binding{std::move(value), mutable_value});
    }
    Binding& resolve(const std::string& name, const Token& token) {
        for (auto* environment = this; environment; environment = environment->parent)
            if (auto found = environment->bindings.find(name); found != environment->bindings.end()) return found->second;
        fail("Имя " + name + " не объявлено.", token);
    }
};
struct List { std::vector<Value> items; bool marked = false; };
struct Function { NodePtr definition; Environment* closure; std::string name; bool marked = false; };
struct Native { std::string name; std::size_t minimum; std::size_t maximum; };
struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};

static std::string type_name(const Value& value) {
    if (std::holds_alternative<Null>(value)) return "null";
    if (std::holds_alternative<Integer>(value)) return "int";
    if (std::holds_alternative<double>(value)) return "float";
    if (std::holds_alternative<bool>(value)) return "bool";
    if (std::holds_alternative<std::string>(value)) return "str";
    if (std::holds_alternative<List*>(value)) return "list";
    return "function";
}
static bool numeric(const Value& value) { return std::holds_alternative<Integer>(value) || std::holds_alternative<double>(value); }
static bool truth(const Value& value) {
    if (std::holds_alternative<Null>(value)) return false;
    if (const auto* n = std::get_if<Integer>(&value)) return *n != 0;
    if (const auto* n = std::get_if<double>(&value)) return *n != 0;
    if (const auto* b = std::get_if<bool>(&value)) return *b;
    if (const auto* s = std::get_if<std::string>(&value)) return !s->empty();
    if (const auto* list = std::get_if<List*>(&value)) return !(*list)->items.empty();
    return true;
}
static Value checked(Integer value, const Token& token) {
    if (integer_bits(value) > max_integer_bits) fail("Целое число превышает 4096 бит.", token, Category::limit);
    return value;
}
static Value checked(double value, const Token& token) {
    if (!std::isfinite(value)) fail("Результат float должен быть конечным.", token);
    return value;
}
static double real_number(const Value& value, const Token& token) {
    if (const auto* integer = std::get_if<Integer>(&value)) return to_double(*integer, token);
    if (const auto* real = std::get_if<double>(&value)) return *real;
    fail("Ожидается число; bool не является числом.", token);
}
static int numeric_compare(const Value& a, const Value& b) {
    if (const auto* left = std::get_if<Integer>(&a)) {
        Integer x = *left, y;
        if (const auto* right = std::get_if<Integer>(&b)) y = *right;
        else { const auto [n, d] = double_ratio(std::get<double>(b)); x *= d; y = n; }
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (std::holds_alternative<Integer>(b)) return -numeric_compare(b, a);
    const double x = std::get<double>(a), y = std::get<double>(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

struct PairHash {
    std::size_t operator()(const std::pair<List*, List*>& pair) const noexcept {
        return std::hash<List*>{}(pair.first) ^ (std::hash<List*>{}(pair.second) << 1);
    }
};
static bool equal(const Value& a, const Value& b, const Token& token,
                  std::unordered_set<std::pair<List*, List*>, PairHash>& seen, std::size_t depth = 0) {
    if (depth > 256) fail("Слишком глубокие значения при сравнении.", token, Category::limit);
    if (numeric(a) && numeric(b)) return numeric_compare(a, b) == 0;
    if (a.index() != b.index()) return false;
    if (std::holds_alternative<Null>(a)) return true;
    if (const auto* x = std::get_if<bool>(&a)) return *x == std::get<bool>(b);
    if (const auto* x = std::get_if<std::string>(&a)) return *x == std::get<std::string>(b);
    if (const auto* x = std::get_if<Function*>(&a)) return *x == std::get<Function*>(b);
    if (const auto* x = std::get_if<Native*>(&a)) return *x == std::get<Native*>(b);
    auto* x = std::get<List*>(a); auto* y = std::get<List*>(b);
    if (x == y) return true;
    if (x->items.size() != y->items.size()) return false;
    if (!seen.emplace(x, y).second) return true;
    for (std::size_t i = 0; i < x->items.size(); ++i)
        if (!equal(x->items[i], y->items[i], token, seen, depth + 1)) return false;
    return true;
}

class Formatter {
    const Token& token_;
    std::size_t limit_;
    std::unordered_set<List*> active_;
public:
    std::string text;
    Formatter(const Token& token, std::size_t limit) : token_(token), limit_(limit) {}
    void append(std::string_view part) {
        if (part.size() > limit_ - text.size()) fail("Превышен лимит размера строкового представления или вывода.", token_, Category::limit);
        text.append(part);
    }
    void value(const Value& item, bool quoted = false, std::size_t depth = 0) {
        if (depth > 256) fail("Слишком глубокое значение для вывода.", token_, Category::limit);
        if (std::holds_alternative<Null>(item)) append("null");
        else if (const auto* n = std::get_if<Integer>(&item)) append(n->convert_to<std::string>());
        else if (const auto* n = std::get_if<double>(&item)) append(float_text(*n));
        else if (const auto* b = std::get_if<bool>(&item)) append(*b ? "true" : "false");
        else if (const auto* s = std::get_if<std::string>(&item)) {
            if (!quoted) { append(*s); return; }
            append("\"");
            static constexpr char hex[] = "0123456789abcdef";
            for (unsigned char c : *s) {
                if (c == '"') append("\\\"");
                else if (c == '\\') append("\\\\");
                else if (c == '\n') append("\\n");
                else if (c == '\r') append("\\r");
                else if (c == '\t') append("\\t");
                else if (c < 32) { std::string escape = "\\u00"; escape += hex[c >> 4]; escape += hex[c & 15]; append(escape); }
                else { const char byte = static_cast<char>(c); append(std::string_view(&byte, 1)); }
            }
            append("\"");
        } else if (const auto* list = std::get_if<List*>(&item)) {
            if (!active_.insert(*list).second) { append("[...]"); return; }
            append("[");
            for (std::size_t i = 0; i < (*list)->items.size(); ++i) {
                if (i) append(", ");
                value((*list)->items[i], true, depth + 1);
            }
            append("]"); active_.erase(*list);
        } else if (const auto* function = std::get_if<Function*>(&item)) {
            append("<function " + (*function)->name + "/" + std::to_string((*function)->definition->parameters.size()) + ">");
        } else append("<builtin " + std::get<Native*>(item)->name + ">");
    }
};

} // namespace fc::detail

namespace fc {
using namespace detail;

struct Runtime::Impl {
    std::istream& input;
    std::ostream& output;
    Limits limits;
    std::uint64_t step_count = 0;
    std::size_t written = 0, call_depth = 0, evaluation_depth = 0;
    Token last_token;
    Value last_value = Null{};
    Environment* globals = nullptr;
    std::vector<std::unique_ptr<Environment>> environments;
    std::vector<std::unique_ptr<List>> lists;
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<Native>> natives;

    Impl(std::istream& in, std::ostream& out, Limits settings) : input(in), output(out), limits(settings) {
        if (!limits.steps || !limits.call_depth || !limits.output_bytes) throw std::invalid_argument("Лимиты должны быть положительными.");
        globals = environment(nullptr);
        const std::vector<Native> definitions = {
            {"print", 0, 255}, {"input", 0, 1}, {"len", 1, 1}, {"append", 2, 2}, {"pop", 1, 2},
            {"str", 1, 1}, {"int", 1, 1}, {"float", 1, 1}, {"range", 1, 3}, {"type", 1, 1}, {"assert", 1, 2}
        };
        for (const auto& definition : definitions) {
            natives.push_back(std::make_unique<Native>(definition));
            globals->define(definition.name, natives.back().get(), {}, false);
        }
    }
    Environment* environment(Environment* parent) {
        auto value = std::make_unique<Environment>(); value->parent = parent;
        auto* pointer = value.get(); environments.push_back(std::move(value)); return pointer;
    }
    List* list(std::vector<Value> items = {}) {
        auto value = std::make_unique<List>(); value->items = std::move(items);
        auto* pointer = value.get(); lists.push_back(std::move(value)); return pointer;
    }
    Function* function(NodePtr definition, Environment* closure, std::string name = "<anonymous>") {
        auto value = std::make_unique<Function>(Function{std::move(definition), closure, std::move(name), false});
        auto* pointer = value.get(); functions.push_back(std::move(value)); return pointer;
    }
    void tick(const Token& token) {
        last_token = token;
        if (step_count >= limits.steps) fail("Превышен лимит " + std::to_string(limits.steps) + " шагов.", token, Category::limit);
        ++step_count;
    }

    // Objects belong to this runtime. Tracing between commands reclaims cycles;
    // collection never runs while temporary evaluation values are on the C++ stack.
    void collect() {
        for (auto& item : environments) item->marked = false;
        for (auto& item : lists) item->marked = false;
        for (auto& item : functions) item->marked = false;
        std::vector<Environment*> pending_environments{globals};
        std::vector<const Value*> pending_values{&last_value};
        while (!pending_environments.empty() || !pending_values.empty()) {
            if (!pending_environments.empty()) {
                auto* current = pending_environments.back(); pending_environments.pop_back();
                if (!current || current->marked) continue;
                current->marked = true;
                pending_environments.push_back(current->parent);
                for (const auto& [name, binding] : current->bindings) { (void)name; pending_values.push_back(&binding.value); }
            } else {
                const auto* current = pending_values.back(); pending_values.pop_back();
                if (const auto* array = std::get_if<List*>(current)) {
                    if ((*array)->marked) continue;
                    (*array)->marked = true;
                    for (const auto& item : (*array)->items) pending_values.push_back(&item);
                } else if (const auto* callable = std::get_if<Function*>(current)) {
                    if ((*callable)->marked) continue;
                    (*callable)->marked = true;
                    pending_environments.push_back((*callable)->closure);
                }
            }
        }
        std::erase_if(functions, [](const auto& item) { return !item->marked; });
        std::erase_if(lists, [](const auto& item) { return !item->marked; });
        std::erase_if(environments, [](const auto& item) { return !item->marked; });
    }
    void block(const NodePtr& body, Environment* scope) {
        for (const auto& statement : body->children) execute(statement, scope);
    }
    Value execute(const NodePtr& statement, Environment* scope) {
        DepthGuard depth(evaluation_depth, statement->token, 1024);
        tick(statement->token);
        const auto& parts = statement->children;
        switch (statement->kind) {
            case N::Let: scope->define(statement->name, evaluate(parts[0], scope), statement->token); break;
            case N::FunctionDeclaration: scope->define(statement->name, function(parts[0], scope, statement->name), statement->token); break;
            case N::Assign: {
                Value value = evaluate(parts[1], scope);
                const auto& target = parts[0];
                if (target->kind == N::Name) {
                    auto& binding = scope->resolve(target->name, target->token);
                    if (!binding.mutable_value) fail("Встроенную функцию нельзя переопределить.", target->token);
                    binding.value = std::move(value);
                } else {
                    Value container = evaluate(target->children[0], scope);
                    Value key = evaluate(target->children[1], scope);
                    const auto* array = std::get_if<List*>(&container);
                    if (!array) fail("Изменять по индексу можно только список; строки неизменяемы.", target->token);
                    (*array)->items[index(key, (*array)->items.size(), target->token)] = std::move(value);
                }
                break;
            }
            case N::ExpressionStatement: return evaluate(parts[0], scope);
            case N::Block: block(statement, environment(scope)); break;
            case N::If:
                if (truth(evaluate(parts[0], scope))) execute(parts[1], scope);
                else if (parts.size() == 3) execute(parts[2], scope);
                break;
            case N::While:
                while (truth(evaluate(parts[0], scope))) {
                    try { execute(parts[1], scope); }
                    catch (const ContinueSignal&) { continue; }
                    catch (const BreakSignal&) { break; }
                }
                break;
            case N::For: {
                const Value iterable = evaluate(parts[0], scope);
                std::vector<Value> snapshot;
                if (const auto* array = std::get_if<List*>(&iterable)) snapshot = (*array)->items;
                else if (const auto* text = std::get_if<std::string>(&iterable)) {
                    for (const auto c : decode_utf8(*text)) snapshot.emplace_back(encode_utf8(std::u32string_view(&c, 1)));
                } else fail("for ожидает строку или список; для чисел используй range.", statement->token);
                for (const auto& value : snapshot) {
                    tick(statement->token);
                    auto* iteration = environment(scope); iteration->define(statement->name, value, statement->token);
                    try { block(parts[1], iteration); }
                    catch (const ContinueSignal&) { continue; }
                    catch (const BreakSignal&) { break; }
                }
                break;
            }
            case N::Return: throw ReturnSignal{parts.empty() ? Value(Null{}) : evaluate(parts[0], scope)};
            case N::Break: throw BreakSignal{};
            case N::Continue: throw ContinueSignal{};
            default: fail("Неизвестная инструкция AST.", statement->token);
        }
        return Null{};
    }
    Value evaluate(const NodePtr& expression, Environment* scope) {
        DepthGuard depth(evaluation_depth, expression->token, 1024);
        tick(expression->token);
        const auto& parts = expression->children;
        const auto& token = expression->token;
        switch (expression->kind) {
            case N::Literal: return expression->value;
            case N::Name: return scope->resolve(expression->name, token).value;
            case N::List: {
                std::vector<Value> items; items.reserve(parts.size());
                for (const auto& item : parts) items.push_back(evaluate(item, scope));
                return list(std::move(items));
            }
            case N::Function: return function(expression, scope);
            case N::Unary: {
                const Value operand = evaluate(parts[0], scope);
                if (expression->op == K::Not) return !truth(operand);
                if (!numeric(operand)) fail("Унарный оператор ожидает число, не bool.", token);
                if (expression->op == K::Plus) return operand;
                if (const auto* integer = std::get_if<Integer>(&operand)) return checked(Integer(-*integer), token);
                return checked(-std::get<double>(operand), token);
            }
            case N::Binary: {
                Value left = evaluate(parts[0], scope);
                if (expression->op == K::And) return truth(left) ? evaluate(parts[1], scope) : left;
                if (expression->op == K::Or) return truth(left) ? left : evaluate(parts[1], scope);
                Value right = evaluate(parts[1], scope);
                return binary(expression->op, std::move(left), std::move(right), token);
            }
            case N::Compare: {
                Value left = evaluate(parts[0], scope);
                for (std::size_t i = 0; i < expression->comparisons.size(); ++i) {
                    Value right = evaluate(parts[i + 1], scope);
                    if (!compare(expression->comparisons[i], left, right, token)) return false;
                    left = std::move(right);
                }
                return true;
            }
            case N::Call: {
                Value callee = evaluate(parts[0], scope);
                std::vector<Value> arguments;
                for (std::size_t i = 1; i < parts.size(); ++i) arguments.push_back(evaluate(parts[i], scope));
                return call(callee, arguments, token);
            }
            case N::Index: {
                Value container = evaluate(parts[0], scope);
                Value key = evaluate(parts[1], scope);
                if (const auto* array = std::get_if<List*>(&container)) return (*array)->items[index(key, (*array)->items.size(), token)];
                if (const auto* text = std::get_if<std::string>(&container)) {
                    const auto chars = decode_utf8(*text); const auto character = chars[index(key, chars.size(), token)];
                    return encode_utf8(std::u32string_view(&character, 1));
                }
                fail("Индексировать можно строку или список.", token);
            }
            default: fail("Неизвестное выражение AST.", token);
        }
    }
    static std::size_t index(const Value& value, std::size_t length, const Token& token) {
        const auto* integer = std::get_if<Integer>(&value);
        if (!integer) fail("Индекс должен быть целым числом, не bool.", token);
        Integer adjusted = *integer;
        if (adjusted < 0) adjusted += length;
        if (adjusted < 0 || adjusted >= length) fail("Индекс за границами значения длины " + std::to_string(length) + '.', token);
        return adjusted.convert_to<std::size_t>();
    }
    bool compare(K operation, const Value& left, const Value& right, const Token& token) {
        if (operation == K::Eq || operation == K::Ne) {
            std::unordered_set<std::pair<List*, List*>, PairHash> seen;
            const bool result = equal(left, right, token, seen);
            return operation == K::Eq ? result : !result;
        }
        int order;
        if (numeric(left) && numeric(right)) order = numeric_compare(left, right);
        else if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) order = std::get<std::string>(left).compare(std::get<std::string>(right));
        else fail("Сравнение порядка ожидает два числа или две строки.", token);
        if (operation == K::Lt) return order < 0;
        if (operation == K::Le) return order <= 0;
        if (operation == K::Gt) return order > 0;
        return order >= 0;
    }
    Value binary(K operation, Value left, Value right, const Token& token) {
        if (operation == K::Plus) {
            if (const auto* a = std::get_if<std::string>(&left)) {
                if (const auto* b = std::get_if<std::string>(&right)) {
                    if (decode_utf8(*a).size() + decode_utf8(*b).size() > max_string_chars) fail("Строка слишком длинная.", token, Category::limit);
                    return *a + *b;
                }
            }
            if (const auto* a = std::get_if<List*>(&left)) {
                if (const auto* b = std::get_if<List*>(&right)) {
                    if ((*a)->items.size() + (*b)->items.size() > max_list_items) fail("Список слишком длинный.", token, Category::limit);
                    auto items = (*a)->items; items.insert(items.end(), (*b)->items.begin(), (*b)->items.end()); return list(std::move(items));
                }
            }
        }
        if (operation == K::Mul) {
            if (std::holds_alternative<Integer>(left) && (std::holds_alternative<std::string>(right) || std::holds_alternative<List*>(right))) std::swap(left, right);
            if (const auto* count = std::get_if<Integer>(&right)) {
                if (std::holds_alternative<std::string>(left) || std::holds_alternative<List*>(left)) {
                    const bool is_string = std::holds_alternative<std::string>(left);
                    const std::size_t length = is_string ? decode_utf8(std::get<std::string>(left)).size() : std::get<List*>(left)->items.size();
                    if (!length || *count <= 0) return is_string ? Value(std::string{}) : Value(list());
                    const auto limit = is_string ? max_string_chars : max_list_items;
                    if (*count > limit / length) fail("Результат повторения слишком большой.", token, Category::limit);
                    const auto repeats = count->convert_to<std::size_t>();
                    if (is_string) {
                        const auto& text = std::get<std::string>(left); std::string result; result.reserve(text.size() * repeats);
                        for (std::size_t i = 0; i < repeats; ++i) result += text;
                        return result;
                    }
                    const auto& original = std::get<List*>(left)->items; std::vector<Value> result; result.reserve(original.size() * repeats);
                    for (std::size_t i = 0; i < repeats; ++i) result.insert(result.end(), original.begin(), original.end());
                    return list(std::move(result));
                }
            }
        }
        if (!numeric(left) || !numeric(right)) fail("Операция ожидает числа; bool не является числом.", token);
        if (std::holds_alternative<Integer>(left) && std::holds_alternative<Integer>(right)) {
            const auto& a = std::get<Integer>(left); const auto& b = std::get<Integer>(right);
            switch (operation) {
                case K::Plus: return checked(Integer(a + b), token);
                case K::Minus: return checked(Integer(a - b), token);
                case K::Mul: return checked(Integer(a * b), token);
                case K::Div: return divide_integers(a, b, token);
                case K::FloorDiv: case K::Mod: {
                    if (b == 0) fail("Деление на ноль.", token);
                    Integer quotient = a / b, remainder = a % b;
                    if (remainder != 0 && ((a < 0) != (b < 0))) { --quotient; remainder += b; }
                    return operation == K::FloorDiv ? Value(quotient) : Value(remainder);
                }
                case K::Pow:
                    if (b >= 0) {
                        if (b == 0) return Integer(1);
                        if (a == 0 || a == 1) return a;
                        if (a == -1) return Integer((b & 1) == 0 ? 1 : -1);
                        if (b * (integer_bits(a) - 1) >= max_integer_bits) fail("Степень создаст слишком большое целое число.", token, Category::limit);
                        unsigned exponent = b.convert_to<unsigned>(); Integer base = a, result = 1;
                        while (exponent) { if (exponent & 1) result *= base; exponent >>= 1; if (exponent) base *= base; }
                        return checked(std::move(result), token);
                    }
                    break;
                default: break;
            }
        }
        const double a = real_number(left, token), b = real_number(right, token);
        switch (operation) {
            case K::Plus: return checked(a + b, token);
            case K::Minus: return checked(a - b, token);
            case K::Mul: return checked(a * b, token);
            case K::Div: if (b == 0) fail("Деление на ноль.", token); return checked(a / b, token);
            case K::FloorDiv: case K::Mod: {
                if (b == 0) fail("Деление на ноль.", token);
                double remainder = std::fmod(a, b), quotient = (a - remainder) / b;
                if (remainder != 0) { if (std::signbit(remainder) != std::signbit(b)) { remainder += b; quotient -= 1; } }
                else remainder = std::copysign(0.0, b);
                if (operation == K::Mod) return checked(remainder, token);
                double result = quotient == 0 ? std::copysign(0.0, a / b) : std::floor(quotient);
                if (quotient != 0 && quotient - result > 0.5) result += 1;
                return checked(result, token);
            }
            case K::Pow:
                if (a == 0 && b < 0) fail("Деление на ноль.", token);
                if (a < 0 && std::floor(b) != b) fail("Комплексные числа не поддерживаются.", token);
                return checked(std::pow(a, b), token);
            default: fail("Неизвестная числовая операция.", token);
        }
    }
    Value call(const Value& callee, const std::vector<Value>& arguments, const Token& token) {
        if (const auto* builtin = std::get_if<Native*>(&callee)) {
            if (arguments.size() < (*builtin)->minimum || arguments.size() > (*builtin)->maximum)
                fail((*builtin)->name + ": неверное число аргументов.", token);
            return native((*builtin)->name, arguments, token);
        }
        const auto* user = std::get_if<Function*>(&callee);
        if (!user) fail("Значение типа " + type_name(callee) + " нельзя вызвать как функцию.", token);
        const auto* callable = *user;
        if (arguments.size() != callable->definition->parameters.size()) fail(callable->name + ": неверное число аргументов.", token);
        DepthGuard depth(call_depth, token, limits.call_depth);
        auto* local = environment(callable->closure);
        for (std::size_t i = 0; i < arguments.size(); ++i) local->define(callable->definition->parameters[i].text, arguments[i], token);
        try { block(callable->definition->children[0], local); }
        catch (ReturnSignal& result) { return std::move(result.value); }
        catch (Error& error) { error.add_frame(callable->name, site(token)); throw; }
        return Null{};
    }
    void write(std::string_view text, const Token& token) {
        if (text.size() > limits.output_bytes - written) fail("Превышен лимит вывода.", token, Category::limit);
        output.write(text.data(), static_cast<std::streamsize>(text.size())); output.flush();
        if (!output) fail("Не удалось записать вывод.", token);
        written += text.size();
    }
    Value native(const std::string& name, const std::vector<Value>& args, const Token& token) {
        if (name == "print") {
            Formatter formatter(token, limits.output_bytes - written);
            for (std::size_t i = 0; i < args.size(); ++i) { if (i) formatter.append(" "); formatter.value(args[i]); }
            formatter.append("\n"); write(formatter.text, token); return Null{};
        }
        if (name == "input") {
            if (!args.empty()) {
                const auto* prompt = std::get_if<std::string>(&args[0]);
                if (!prompt) fail("Подсказка input должна быть строкой.", token);
                write(*prompt, token);
            }
            std::string text; bool any = false, newline = false; char byte;
            while (input.get(byte)) {
                any = true;
                if (byte == '\n') { newline = true; break; }
                text += byte;
                if (text.size() > max_string_chars * 4 + 1) fail("Входная строка слишком длинная.", token, Category::limit);
            }
            if (input.bad()) fail("Не удалось прочитать ввод.", token);
            if (!any) return Null{};
            if (newline && !text.empty() && text.back() == '\r') text.pop_back();
            try { (void)decode_utf8(text); }
            catch (const Error& error) { fail(error.what(), token, error.category() == Category::limit ? Category::limit : Category::runtime); }
            return text;
        }
        if (name == "type") return type_name(args[0]);
        if (name == "len") {
            if (const auto* text = std::get_if<std::string>(&args[0])) return Integer(decode_utf8(*text).size());
            if (const auto* array = std::get_if<List*>(&args[0])) return Integer((*array)->items.size());
            fail("len ожидает строку или список.", token);
        }
        if (name == "append" || name == "pop") {
            const auto* array = std::get_if<List*>(&args[0]);
            if (!array) fail(name + " ожидает список.", token);
            auto& items = (*array)->items;
            if (name == "append") {
                if (items.size() >= max_list_items) fail("Список достиг максимальной длины.", token, Category::limit);
                items.push_back(args[1]); return Null{};
            }
            const auto position = index(args.size() == 2 ? args[1] : Value(Integer(-1)), items.size(), token);
            Value result = items[position]; items.erase(items.begin() + static_cast<std::ptrdiff_t>(position)); return result;
        }
        if (name == "str") {
            Formatter formatter(token, max_string_chars * 4); formatter.value(args[0]);
            (void)decode_utf8(formatter.text); return formatter.text;
        }
        if (name == "int") {
            if (const auto* integer = std::get_if<Integer>(&args[0])) return *integer;
            if (const auto* real = std::get_if<double>(&args[0])) { const auto [n, d] = double_ratio(*real); return checked(Integer(n / d), token); }
            if (const auto* boolean = std::get_if<bool>(&args[0])) return Integer(*boolean ? 1 : 0);
            if (const auto* text = std::get_if<std::string>(&args[0])) return decimal_integer(*text, token, Category::runtime);
            fail("int ожидает строку, число или bool.", token);
        }
        if (name == "float") {
            if (numeric(args[0])) return checked(real_number(args[0], token), token);
            if (const auto* boolean = std::get_if<bool>(&args[0])) return *boolean ? 1.0 : 0.0;
            if (const auto* text = std::get_if<std::string>(&args[0])) {
                std::string_view view(*text);
                while (!view.empty() && (view.front() == ' ' || view.front() == '\t' || view.front() == '\n' || view.front() == '\r')) view.remove_prefix(1);
                while (!view.empty() && (view.back() == ' ' || view.back() == '\t' || view.back() == '\n' || view.back() == '\r')) view.remove_suffix(1);
                if (!view.empty() && view.front() == '+') view.remove_prefix(1);
                double value = 0;
                const auto result = std::from_chars(view.data(), view.data() + view.size(), value, std::chars_format::general);
                if (view.empty() || result.ec != std::errc{} || result.ptr != view.data() + view.size()) fail("Некорректная запись float.", token);
                return checked(value, token);
            }
            fail("float ожидает строку, число или bool.", token);
        }
        if (name == "range") {
            for (const auto& value : args) if (!std::holds_alternative<Integer>(value)) fail("range ожидает целые числа.", token);
            Integer start = args.size() == 1 ? Integer(0) : std::get<Integer>(args[0]);
            Integer stop = std::get<Integer>(args[args.size() == 1 ? 0 : 1]);
            Integer step = args.size() == 3 ? std::get<Integer>(args[2]) : Integer(1);
            if (step == 0) fail("Шаг range не может равняться нулю.", token);
            Integer count = 0;
            if (step > 0 && start < stop) count = (stop - start - 1) / step + 1;
            if (step < 0 && start > stop) count = (start - stop - 1) / (-step) + 1;
            if (count > max_list_items) fail("range создаст больше 100 000 элементов.", token, Category::limit);
            const auto size = count.convert_to<std::size_t>(); std::vector<Value> items; items.reserve(size);
            for (std::size_t i = 0; i < size; ++i, start += step) items.emplace_back(start);
            return list(std::move(items));
        }
        if (name == "assert") {
            if (!truth(args[0])) {
                Formatter formatter(token, 4096);
                if (args.size() == 2) formatter.value(args[1]); else formatter.append("Проверка assert не выполнена.");
                fail(formatter.text, token);
            }
            return Null{};
        }
        fail("Неизвестная встроенная функция.", token);
    }
};

Runtime::Runtime(std::istream& input, std::ostream& output, Limits limits) : impl_(std::make_unique<Impl>(input, output, limits)) {}
Runtime::~Runtime() = default;
void Runtime::run(const Program& program) {
    if (!program.data) throw std::invalid_argument("Ожидается программа, созданная parse().");
    impl_->last_value = Null{};
    try {
        for (const auto& statement : program.data->statements) impl_->last_value = impl_->execute(statement, impl_->globals);
    } catch (...) { impl_->collect(); throw; }
    impl_->collect();
}
void Runtime::reset_budget() { impl_->step_count = 0; impl_->written = 0; impl_->last_value = Null{}; }
std::string Runtime::last_value_text() const {
    Formatter formatter(impl_->last_token, max_string_chars * 4); formatter.value(impl_->last_value); return formatter.text;
}
std::string Runtime::last_value_type() const { return type_name(impl_->last_value); }
bool Runtime::last_value_is_null() const { return std::holds_alternative<Null>(impl_->last_value); }
void Runtime::echo_last() {
    Formatter formatter(impl_->last_token, impl_->limits.output_bytes - impl_->written);
    formatter.value(impl_->last_value); formatter.append("\n"); impl_->write(formatter.text, impl_->last_token);
}
std::uint64_t Runtime::steps() const noexcept { return impl_->step_count; }
std::size_t Runtime::output_bytes() const noexcept { return impl_->written; }
std::size_t Runtime::live_objects() const noexcept { return impl_->environments.size() + impl_->lists.size() + impl_->functions.size() + impl_->natives.size(); }
Result capture(std::string_view source, std::string input, Limits limits) {
    std::istringstream in(std::move(input)); std::ostringstream out;
    Runtime runtime(in, out, limits); runtime.run(parse(source));
    return {out.str(), runtime.last_value_text(), runtime.last_value_type(), runtime.steps()};
}

} // namespace fc
