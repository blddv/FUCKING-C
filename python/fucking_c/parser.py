from __future__ import annotations

from dataclasses import dataclass

from .errors import FCError
from .lexer import MAX_LIST_ITEMS, Source, Token, tokenize


@dataclass(slots=True)
class Node:
    token: Token


@dataclass(slots=True)
class Literal(Node):
    value: object


@dataclass(slots=True)
class Name(Node):
    name: str


@dataclass(slots=True)
class Unary(Node):
    op: str
    operand: Node


@dataclass(slots=True)
class Binary(Node):
    left: Node
    op: str
    right: Node


@dataclass(slots=True)
class Compare(Node):
    left: Node
    pairs: list[tuple[str, Node]]


@dataclass(slots=True)
class Call(Node):
    callee: Node
    arguments: list[Node]


@dataclass(slots=True)
class Index(Node):
    container: Node
    index: Node


@dataclass(slots=True)
class ListLiteral(Node):
    items: list[Node]


@dataclass(slots=True)
class Function(Node):
    parameters: list[Token]
    body: Block


@dataclass(slots=True)
class Block(Node):
    statements: list[Node]


@dataclass(slots=True)
class Let(Node):
    name: Token
    value: Node


@dataclass(slots=True)
class FunctionDeclaration(Node):
    name: Token
    function: Function


@dataclass(slots=True)
class Assign(Node):
    target: Name | Index
    value: Node


@dataclass(slots=True)
class ExpressionStatement(Node):
    expression: Node


@dataclass(slots=True)
class If(Node):
    condition: Node
    then_branch: Block
    else_branch: Block | If | None


@dataclass(slots=True)
class While(Node):
    condition: Node
    body: Block


@dataclass(slots=True)
class For(Node):
    name: Token
    iterable: Node
    body: Block


@dataclass(slots=True)
class Return(Node):
    value: Node | None


@dataclass(slots=True)
class Break(Node):
    pass


@dataclass(slots=True)
class Continue(Node):
    pass


@dataclass(slots=True)
class Program:
    statements: list[Node]
    source: Source


_BINDING = {"OR": 10, "AND": 20, "EQ": 40, "NE": 40, "LT": 40, "LE": 40, "GT": 40, "GE": 40,
            "PLUS": 50, "MINUS": 50, "MUL": 60, "DIV": 60, "FLOORDIV": 60, "MOD": 60, "POW": 80}
_COMPARISONS = {"EQ", "NE", "LT", "LE", "GT", "GE"}


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.position = 0
        self.function_depth = 0
        self.loop_depth = 0

    @property
    def current(self) -> Token:
        return self.tokens[self.position]

    def advance(self) -> Token:
        token = self.current
        if token.kind != "EOF":
            self.position += 1
        return token

    def match(self, kind: str) -> Token | None:
        return self.advance() if self.current.kind == kind else None

    def expect(self, kind: str, explanation: str) -> Token:
        if self.current.kind != kind:
            raise FCError(explanation, self.current)
        return self.advance()

    def parse(self) -> Program:
        statements = []
        try:
            while self.current.kind != "EOF":
                statements.append(self.statement())
        except RecursionError:
            raise FCError("Слишком глубокая вложенность синтаксиса.", self.current, "limit") from None
        return Program(statements, self.tokens[0].source)

    def statement(self) -> Node:
        token = self.current
        if self.match("LET"):
            name = self.expect("IDENT", "После C ожидается имя вида ƈC.")
            self.expect("ASSIGN", "После имени ожидается присваивание Ç.")
            value = self.expression()
            self.expect("SEMI", "Объявление заканчивается командой ċ.")
            return Let(token, name, value)
        if token.kind == "FN" and self.tokens[self.position + 1].kind == "IDENT":
            self.advance()
            name = self.advance()
            return FunctionDeclaration(token, name, self.function(token))
        if self.match("IF"):
            return self.if_statement(token)
        if self.match("WHILE"):
            condition = self.condition()
            self.loop_depth += 1
            try:
                body = self.block()
            finally:
                self.loop_depth -= 1
            return While(token, condition, body)
        if self.match("FOR"):
            self.expect("LPAREN", "После CCC ожидается Ć.")
            name = self.expect("IDENT", "Ожидается имя переменной цикла.")
            self.expect("IN", "После переменной цикла ожидается CCc (in).")
            iterable = self.expression()
            self.expect("RPAREN", "Заголовок цикла заканчивается ć.")
            self.loop_depth += 1
            try:
                body = self.block()
            finally:
                self.loop_depth -= 1
            return For(token, name, iterable, body)
        if self.match("RETURN"):
            if not self.function_depth:
                raise FCError("cC (return) допустим только внутри функции.", token)
            value = None if self.current.kind == "SEMI" else self.expression()
            self.expect("SEMI", "Возврат заканчивается командой ċ.")
            return Return(token, value)
        if token.kind in {"BREAK", "CONTINUE"}:
            self.advance()
            if not self.loop_depth:
                raise FCError("break/continue допустимы только внутри собственного цикла.", token)
            self.expect("SEMI", "После break/continue ожидается ċ.")
            return Break(token) if token.kind == "BREAK" else Continue(token)
        if token.kind == "LBRACE":
            return self.block()
        expression = self.expression()
        if self.match("ASSIGN"):
            if not isinstance(expression, (Name, Index)):
                raise FCError("Присваивать можно только переменной или элементу списка.", expression.token)
            value = self.expression()
            self.expect("SEMI", "Присваивание заканчивается командой ċ.")
            return Assign(token, expression, value)
        self.expect("SEMI", "После выражения ожидается ċ.")
        return ExpressionStatement(token, expression)

    def condition(self) -> Node:
        self.expect("LPAREN", "Перед условием ожидается Ć.")
        expression = self.expression()
        self.expect("RPAREN", "После условия ожидается ć.")
        return expression

    def if_statement(self, token: Token) -> If:
        condition = self.condition()
        then_branch = self.block()
        else_branch = None
        if self.match("ELSE"):
            next_if = self.match("IF")
            else_branch = self.if_statement(next_if) if next_if else self.block()
        return If(token, condition, then_branch, else_branch)

    def block(self) -> Block:
        token = self.expect("LBRACE", "Блок начинается с Ĉ.")
        statements = []
        while self.current.kind not in {"RBRACE", "EOF"}:
            statements.append(self.statement())
        self.expect("RBRACE", "Незакрытый блок: ожидается ĉ.")
        return Block(token, statements)

    def function(self, token: Token) -> Function:
        self.expect("LPAREN", "Перед параметрами функции ожидается Ć.")
        parameters = []
        names = set()
        if not self.match("RPAREN"):
            while True:
                parameter = self.expect("IDENT", "Параметр функции должен быть именем вида ƈC.")
                if parameter.value in names:
                    raise FCError("Параметр указан повторно.", parameter)
                names.add(parameter.value)
                parameters.append(parameter)
                if len(parameters) > 255:
                    raise FCError("У функции не может быть больше 255 параметров.", parameter, "limit")
                if self.match("RPAREN"):
                    break
                self.expect("COMMA", "Параметры разделяются Ċ.")
                if self.match("RPAREN"):
                    break
        outer_loop_depth = self.loop_depth
        self.loop_depth = 0
        self.function_depth += 1
        try:
            body = self.block()
        finally:
            self.function_depth -= 1
            self.loop_depth = outer_loop_depth
        return Function(token, parameters, body)

    def expression(self, minimum: int = 0) -> Node:
        token = self.advance()
        if token.kind in {"NUMBER", "STRING"}:
            left: Node = Literal(token, token.value)
        elif token.kind in {"TRUE", "FALSE", "NULL"}:
            left = Literal(token, {"TRUE": True, "FALSE": False, "NULL": None}[token.kind])
        elif token.kind in {"IDENT", "BUILTIN"}:
            left = Name(token, str(token.value))
        elif token.kind in {"MINUS", "PLUS", "NOT"}:
            left = Unary(token, token.kind, self.expression(35 if token.kind == "NOT" else 70))
        elif token.kind == "LPAREN":
            left = self.expression()
            self.expect("RPAREN", "Незакрытая группа: ожидается ć.")
        elif token.kind == "LBRACKET":
            left = ListLiteral(token, self.sequence("RBRACKET", MAX_LIST_ITEMS))
        elif token.kind == "FN":
            left = self.function(token)
        else:
            raise FCError("Ожидается выражение: число, строка, имя, список или вызов.", token)

        while True:
            operator = self.current
            if operator.kind == "LPAREN" and minimum <= 90:
                self.advance()
                left = Call(operator, left, self.sequence("RPAREN", 255))
                continue
            if operator.kind == "LBRACKET" and minimum <= 90:
                self.advance()
                index = self.expression()
                self.expect("RBRACKET", "После индекса ожидается č.")
                left = Index(operator, left, index)
                continue
            binding = _BINDING.get(operator.kind, -1)
            if binding < minimum:
                break
            if operator.kind in _COMPARISONS:
                pairs = []
                while self.current.kind in _COMPARISONS:
                    comparison = self.advance()
                    pairs.append((comparison.kind, self.expression(41)))
                left = Compare(operator, left, pairs)
                continue
            self.advance()
            right = self.expression(binding if operator.kind == "POW" else binding + 1)
            left = Binary(operator, left, operator.kind, right)
        return left

    def sequence(self, closing: str, limit: int) -> list[Node]:
        items = []
        if self.match(closing):
            return items
        while True:
            items.append(self.expression())
            if len(items) > limit:
                raise FCError(f"Не больше {limit} элементов в этой конструкции.", self.current, "limit")
            if self.match(closing):
                break
            self.expect("COMMA", "Элементы разделяются Ċ; проверь закрывающую скобку.")
            if self.match(closing):
                break
        return items


def parse(text: str, name: str = "<code>") -> Program:
    return Parser(tokenize(text, name)).parse()
