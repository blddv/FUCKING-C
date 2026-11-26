from __future__ import annotations

from dataclasses import dataclass
import io
import json
import math
import re
import sys
from typing import Callable, TextIO

from .errors import FCError
from .lexer import MAX_INTEGER_BITS, MAX_LIST_ITEMS, MAX_STRING_CHARS, Token
from . import parser as ast


@dataclass(slots=True)
class Binding:
    value: object
    mutable: bool = True


class Environment:
    def __init__(self, parent: Environment | None = None):
        self.parent = parent
        self.bindings: dict[str, Binding] = {}

    def define(self, name: str, value: object, token: Token | None = None, mutable: bool = True) -> None:
        if name in self.bindings:
            raise FCError(f"Имя {name} уже объявлено в этой области.", token, "runtime")
        self.bindings[name] = Binding(value, mutable)

    def resolve(self, name: str, token: Token) -> Binding:
        environment: Environment | None = self
        while environment is not None:
            if name in environment.bindings:
                return environment.bindings[name]
            environment = environment.parent
        raise FCError(f"Имя {name} не объявлено.", token, "runtime")

    def get(self, name: str, token: Token) -> object:
        return self.resolve(name, token).value

    def assign(self, name: str, value: object, token: Token) -> None:
        binding = self.resolve(name, token)
        if not binding.mutable:
            raise FCError("Встроенную функцию нельзя переопределить.", token, "runtime")
        binding.value = value


@dataclass(slots=True, eq=False)
class UserFunction:
    parameters: list[Token]
    body: ast.Block
    closure: Environment
    name: str = "<anonymous>"


@dataclass(slots=True, eq=False)
class NativeFunction:
    name: str
    minimum: int
    maximum: int | None
    callback: Callable[[list[object], Token], object]


class _Return(Exception):
    def __init__(self, value: object):
        self.value = value


class _Break(Exception):
    pass


class _Continue(Exception):
    pass


def value_type(value: object) -> str:
    if value is None:
        return "null"
    if isinstance(value, (UserFunction, NativeFunction)):
        return "function"
    return {bool: "bool", int: "int", float: "float", str: "str", list: "list"}.get(type(value), "unknown")


def display(value: object, nested: bool = False, seen: set[int] | None = None) -> str:
    if value is None:
        return "null"
    if type(value) is bool:
        return "true" if value else "false"
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False) if nested else value
    if isinstance(value, list):
        seen = set() if seen is None else seen
        identity = id(value)
        if identity in seen:
            return "[...]"
        seen.add(identity)
        try:
            return "[" + ", ".join(display(item, True, seen) for item in value) + "]"
        finally:
            seen.remove(identity)
    if isinstance(value, UserFunction):
        return f"<function {value.name}/{len(value.parameters)}>"
    if isinstance(value, NativeFunction):
        return f"<builtin {value.name}>"
    return str(value)


def _is_number(value: object) -> bool:
    return type(value) in (int, float)


def _equal(left: object, right: object, seen: set[tuple[int, int]] | None = None) -> bool:
    if _is_number(left) and _is_number(right):
        return left == right
    if type(left) is not type(right):
        return False
    if isinstance(left, list):
        if left is right:
            return True
        if len(left) != len(right):
            return False
        seen = set() if seen is None else seen
        pair = (id(left), id(right))
        if pair in seen:
            return True
        seen.add(pair)
        return all(_equal(a, b, seen) for a, b in zip(left, right))
    if isinstance(left, (UserFunction, NativeFunction)):
        return left is right
    return left == right


class Runtime:
    def __init__(self, *, stdin: TextIO | None = None, stdout: TextIO | None = None,
                 max_steps: int = 1_000_000, max_depth: int = 128, max_output: int = 1_048_576):
        for name, value in (("max_steps", max_steps), ("max_depth", max_depth), ("max_output", max_output)):
            if type(value) is not int or value < 1:
                raise ValueError(f"{name} должен быть положительным целым числом.")
        self.stdin = sys.stdin if stdin is None else stdin
        self.stdout = sys.stdout if stdout is None else stdout
        self.max_steps = max_steps
        self.max_depth = max_depth
        self.max_output = max_output
        self.steps = 0
        self.call_depth = 0
        self.output_bytes = 0
        self.last_value: object = None
        self.last_token: Token | None = None
        self.globals = Environment()
        natives = [
            NativeFunction("print", 0, None, self._print),
            NativeFunction("input", 0, 1, self._input),
            NativeFunction("len", 1, 1, self._len),
            NativeFunction("append", 2, 2, self._append),
            NativeFunction("pop", 1, 2, self._pop),
            NativeFunction("str", 1, 1, self._str),
            NativeFunction("int", 1, 1, self._int),
            NativeFunction("float", 1, 1, self._float),
            NativeFunction("range", 1, 3, self._range),
            NativeFunction("type", 1, 1, lambda args, token: value_type(args[0])),
            NativeFunction("assert", 1, 2, self._assert),
        ]
        for native in natives:
            self.globals.define(native.name, native, mutable=False)

    def reset_budget(self) -> None:
        self.steps = 0
        self.output_bytes = 0
        self.last_value = None

    def tick(self, token: Token) -> None:
        self.last_token = token
        if self.steps >= self.max_steps:
            raise FCError(f"Превышен лимит {self.max_steps} шагов выполнения.", token, "limit")
        self.steps += 1

    def run(self, program: ast.Program) -> object:
        try:
            self.last_value = None
            for statement in program.statements:
                self.last_value = self.execute(statement, self.globals)
            return self.last_value
        except RecursionError:
            raise FCError("Слишком глубокий синтаксис, вызовы или вложенные значения.", self.last_token, "limit") from None

    def execute(self, node: ast.Node, environment: Environment) -> object:
        self.tick(node.token)
        if isinstance(node, ast.Let):
            environment.define(str(node.name.value), self.evaluate(node.value, environment), node.name)
        elif isinstance(node, ast.FunctionDeclaration):
            function = UserFunction(node.function.parameters, node.function.body, environment, str(node.name.value))
            environment.define(str(node.name.value), function, node.name)
        elif isinstance(node, ast.Assign):
            value = self.evaluate(node.value, environment)
            if isinstance(node.target, ast.Name):
                environment.assign(node.target.name, value, node.target.token)
            else:
                container = self.evaluate(node.target.container, environment)
                index = self.evaluate(node.target.index, environment)
                if not isinstance(container, list):
                    raise FCError("Изменять по индексу можно только список; строки неизменяемы.", node.target.token, "runtime")
                self.check_index(container, index, node.target.token)
                container[index] = value
        elif isinstance(node, ast.ExpressionStatement):
            return self.evaluate(node.expression, environment)
        elif isinstance(node, ast.Block):
            self.execute_block(node, Environment(environment))
        elif isinstance(node, ast.If):
            if self.evaluate(node.condition, environment):
                self.execute(node.then_branch, environment)
            elif node.else_branch is not None:
                self.execute(node.else_branch, environment)
        elif isinstance(node, ast.While):
            while self.evaluate(node.condition, environment):
                try:
                    self.execute(node.body, environment)
                except _Continue:
                    continue
                except _Break:
                    break
        elif isinstance(node, ast.For):
            iterable = self.evaluate(node.iterable, environment)
            if not isinstance(iterable, (str, list)):
                raise FCError("for ожидает строку или список; используй range для чисел.", node.token, "runtime")
            for value in list(iterable):
                self.tick(node.token)
                iteration = Environment(environment)
                iteration.define(str(node.name.value), value, node.name)
                try:
                    self.execute_block(node.body, iteration)
                except _Continue:
                    continue
                except _Break:
                    break
        elif isinstance(node, ast.Return):
            raise _Return(None if node.value is None else self.evaluate(node.value, environment))
        elif isinstance(node, ast.Break):
            raise _Break()
        elif isinstance(node, ast.Continue):
            raise _Continue()
        else:
            raise FCError("Неизвестный узел инструкции.", node.token, "runtime")
        return None

    def execute_block(self, block: ast.Block, environment: Environment) -> None:
        for statement in block.statements:
            self.execute(statement, environment)

    def evaluate(self, node: ast.Node, environment: Environment) -> object:
        self.tick(node.token)
        if isinstance(node, ast.Literal):
            return node.value
        if isinstance(node, ast.Name):
            return environment.get(node.name, node.token)
        if isinstance(node, ast.ListLiteral):
            return [self.evaluate(item, environment) for item in node.items]
        if isinstance(node, ast.Function):
            return UserFunction(node.parameters, node.body, environment)
        if isinstance(node, ast.Unary):
            operand = self.evaluate(node.operand, environment)
            if node.op == "NOT":
                return not operand
            self.require_numbers(operand, operand, node.token)
            return self.guard(-operand if node.op == "MINUS" else operand, node.token)
        if isinstance(node, ast.Binary):
            left = self.evaluate(node.left, environment)
            if node.op == "AND":
                return self.evaluate(node.right, environment) if left else left
            if node.op == "OR":
                return left if left else self.evaluate(node.right, environment)
            right = self.evaluate(node.right, environment)
            return self.binary(node.op, left, right, node.token)
        if isinstance(node, ast.Compare):
            left = self.evaluate(node.left, environment)
            for operation, expression in node.pairs:
                right = self.evaluate(expression, environment)
                if not self.compare(operation, left, right, node.token):
                    return False
                left = right
            return True
        if isinstance(node, ast.Call):
            callee = self.evaluate(node.callee, environment)
            arguments = [self.evaluate(argument, environment) for argument in node.arguments]
            return self.call(callee, arguments, node.token)
        if isinstance(node, ast.Index):
            container = self.evaluate(node.container, environment)
            index = self.evaluate(node.index, environment)
            if not isinstance(container, (str, list)):
                raise FCError("Индексировать можно только строку или список.", node.token, "runtime")
            self.check_index(container, index, node.token)
            return container[index]
        raise FCError("Неизвестный узел выражения.", node.token, "runtime")

    @staticmethod
    def check_index(container: list | str, index: object, token: Token) -> None:
        if type(index) is not int:
            raise FCError("Индекс должен быть целым числом, не bool.", token, "runtime")
        if not -len(container) <= index < len(container):
            raise FCError(f"Индекс {index} за границами значения длины {len(container)}.", token, "runtime")

    @staticmethod
    def require_numbers(left: object, right: object, token: Token) -> None:
        if not _is_number(left) or not _is_number(right):
            raise FCError("Эта операция ожидает числа; bool не является числом.", token, "runtime")

    @staticmethod
    def guard(value: object, token: Token) -> object:
        if type(value) is int and value.bit_length() > MAX_INTEGER_BITS:
            raise FCError(f"Число превышает {MAX_INTEGER_BITS} бит.", token, "limit")
        if type(value) is float and not math.isfinite(value):
            raise FCError("Результат float должен быть конечным.", token, "runtime")
        if type(value) is complex:
            raise FCError("Комплексные числа не поддерживаются.", token, "runtime")
        if isinstance(value, str) and len(value) > MAX_STRING_CHARS:
            raise FCError("Строка превышает допустимую длину.", token, "limit")
        if isinstance(value, list) and len(value) > MAX_LIST_ITEMS:
            raise FCError("Список превышает допустимую длину.", token, "limit")
        return value

    def binary(self, operation: str, left: object, right: object, token: Token) -> object:
        if operation == "PLUS" and type(left) is type(right) and isinstance(left, (str, list)):
            limit = MAX_STRING_CHARS if isinstance(left, str) else MAX_LIST_ITEMS
            if len(left) + len(right) > limit:
                raise FCError("Результат конкатенации слишком большой.", token, "limit")
            return left + right
        if operation == "MUL":
            if type(left) is int and isinstance(right, (str, list)):
                left, right = right, left
            if isinstance(left, (str, list)) and type(right) is int:
                if not left or right <= 0:
                    return left[:0]
                limit = MAX_STRING_CHARS if isinstance(left, str) else MAX_LIST_ITEMS
                if len(left) * max(0, right) > limit:
                    raise FCError("Результат повторения слишком большой.", token, "limit")
                return left * right
        self.require_numbers(left, right, token)
        try:
            if operation == "PLUS":
                result = left + right
            elif operation == "MINUS":
                result = left - right
            elif operation == "MUL":
                result = left * right
            elif operation == "DIV":
                result = left / right
            elif operation == "FLOORDIV":
                result = left // right
            elif operation == "MOD":
                result = left % right
            elif operation == "POW":
                if type(left) is int and type(right) is int and abs(left) > 1 and right >= 0:
                    if right * (abs(left).bit_length() - 1) >= MAX_INTEGER_BITS:
                        raise FCError("Степень создаст слишком большое целое число.", token, "limit")
                result = left ** right
            else:
                raise FCError("Неизвестный оператор.", token, "runtime")
        except ZeroDivisionError:
            raise FCError("Деление на ноль.", token, "runtime") from None
        except OverflowError:
            raise FCError("Число не помещается в float или результат слишком большой.", token, "limit") from None
        return self.guard(result, token)

    def compare(self, operation: str, left: object, right: object, token: Token) -> bool:
        if operation in {"EQ", "NE"}:
            equal = _equal(left, right)
            return equal if operation == "EQ" else not equal
        if not ((_is_number(left) and _is_number(right)) or (isinstance(left, str) and isinstance(right, str))):
            raise FCError("Сравнение порядка ожидает два числа или две строки.", token, "runtime")
        if operation == "LT":
            return left < right
        if operation == "LE":
            return left <= right
        if operation == "GT":
            return left > right
        return left >= right

    def call(self, callee: object, arguments: list[object], token: Token) -> object:
        if isinstance(callee, NativeFunction):
            if len(arguments) < callee.minimum or (callee.maximum is not None and len(arguments) > callee.maximum):
                maximum = "∞" if callee.maximum is None else str(callee.maximum)
                raise FCError(f"{callee.name}: ожидается {callee.minimum}–{maximum} аргументов, получено {len(arguments)}.", token, "runtime")
            return callee.callback(arguments, token)
        if not isinstance(callee, UserFunction):
            raise FCError(f"Значение типа {value_type(callee)} нельзя вызвать как функцию.", token, "runtime")
        if len(arguments) != len(callee.parameters):
            raise FCError(f"{callee.name}: ожидается {len(callee.parameters)} аргументов, получено {len(arguments)}.", token, "runtime")
        if self.call_depth >= self.max_depth:
            raise FCError(f"Превышена глубина вызовов {self.max_depth}.", token, "limit")
        local = Environment(callee.closure)
        for parameter, argument in zip(callee.parameters, arguments):
            local.define(str(parameter.value), argument, parameter)
        self.call_depth += 1
        try:
            self.execute_block(callee.body, local)
        except _Return as result:
            return result.value
        except FCError as error:
            error.add_frame(callee.name, token)
            raise
        finally:
            self.call_depth -= 1
        return None

    def write(self, text: str, token: Token) -> None:
        size = len(text.encode("utf-8"))
        if self.output_bytes + size > self.max_output:
            raise FCError(f"Вывод превышает {self.max_output} байт UTF-8.", token, "limit")
        self.stdout.write(text)
        self.stdout.flush()
        self.output_bytes += size

    def _print(self, arguments: list[object], token: Token) -> None:
        self.write(" ".join(display(value) for value in arguments) + "\n", token)

    def _input(self, arguments: list[object], token: Token) -> str | None:
        if arguments:
            if not isinstance(arguments[0], str):
                raise FCError("Подсказка input должна быть строкой.", token, "runtime")
            self.write(arguments[0], token)
        line = self.stdin.readline(MAX_STRING_CHARS + 2)
        if len(line) > MAX_STRING_CHARS + 1:
            raise FCError("Входная строка слишком длинная.", token, "limit")
        if not line:
            return None
        if line.endswith("\n"):
            line = line[:-1]
            if line.endswith("\r"):
                line = line[:-1]
        return self.guard(line, token)

    def _len(self, arguments: list[object], token: Token) -> int:
        if not isinstance(arguments[0], (str, list)):
            raise FCError("len ожидает строку или список.", token, "runtime")
        return len(arguments[0])

    def _append(self, arguments: list[object], token: Token) -> None:
        sequence, value = arguments
        if not isinstance(sequence, list):
            raise FCError("append ожидает список.", token, "runtime")
        if len(sequence) >= MAX_LIST_ITEMS:
            raise FCError("Список достиг максимальной длины.", token, "limit")
        sequence.append(value)

    def _pop(self, arguments: list[object], token: Token) -> object:
        sequence = arguments[0]
        if not isinstance(sequence, list):
            raise FCError("pop ожидает список.", token, "runtime")
        index = arguments[1] if len(arguments) == 2 else -1
        self.check_index(sequence, index, token)
        return sequence.pop(index)

    def _str(self, arguments: list[object], token: Token) -> str:
        return self.guard(display(arguments[0]), token)

    def _int(self, arguments: list[object], token: Token) -> int:
        value = arguments[0]
        if isinstance(value, str):
            value = value.strip()
            if len(value) > 1400:
                raise FCError("Слишком длинная запись целого числа.", token, "limit")
            if not re.fullmatch(r"[+-]?[0-9]+", value):
                raise FCError("int ожидает десятичную запись целого числа.", token, "runtime")
        elif type(value) not in (int, float, bool):
            raise FCError("int ожидает строку, число или bool.", token, "runtime")
        return self.guard(int(value), token)

    def _float(self, arguments: list[object], token: Token) -> float:
        value = arguments[0]
        if type(value) not in (str, int, float, bool):
            raise FCError("float ожидает строку, число или bool.", token, "runtime")
        try:
            return self.guard(float(value), token)
        except (ValueError, OverflowError):
            raise FCError("Некорректное или слишком большое число float.", token, "runtime") from None

    def _range(self, arguments: list[object], token: Token) -> list[int]:
        if any(type(value) is not int for value in arguments):
            raise FCError("range ожидает целые числа.", token, "runtime")
        if len(arguments) == 3 and arguments[2] == 0:
            raise FCError("Шаг range не может равняться нулю.", token, "runtime")
        sequence = range(*arguments)
        try:
            size = len(sequence)
        except OverflowError:
            size = MAX_LIST_ITEMS + 1
        if size > MAX_LIST_ITEMS:
            raise FCError(f"range не может создать больше {MAX_LIST_ITEMS} элементов.", token, "limit")
        return list(sequence)

    def _assert(self, arguments: list[object], token: Token) -> None:
        if not arguments[0]:
            message = display(arguments[1]) if len(arguments) == 2 else "Проверка assert не выполнена."
            raise FCError(message, token, "runtime")


def run(source: str, *, name: str = "<code>", stdin: TextIO | None = None,
        stdout: TextIO | None = None, **limits: int) -> Runtime:
    program = ast.parse(source, name)
    runtime = Runtime(stdin=stdin, stdout=stdout, **limits)
    runtime.run(program)
    return runtime


def capture(source: str, input_text: str = "", **limits: int) -> tuple[str, Runtime]:
    output = io.StringIO()
    runtime = run(source, stdin=io.StringIO(input_text), stdout=output, **limits)
    return output.getvalue(), runtime
