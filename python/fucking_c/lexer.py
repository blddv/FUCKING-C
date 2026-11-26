from __future__ import annotations

from dataclasses import dataclass
import math
import re
import unicodedata

from .errors import FCError

MAX_SOURCE_CHARS = 1_000_000
MAX_INTEGER_BITS = 4096
MAX_STRING_CHARS = 1_000_000
MAX_LIST_ITEMS = 100_000

SYMBOLS = {
    "C": "LET", "c": "FN", "CC": "IF", "cc": "ELSE",
    "Cc": "WHILE", "cC": "RETURN", "CCC": "FOR", "CCc": "IN",
    "CcC": "BREAK", "Ccc": "CONTINUE",
    "cCC": "AND", "cCc": "OR", "ccC": "NOT",
    "Ć": "LPAREN", "ć": "RPAREN", "Ĉ": "LBRACE", "ĉ": "RBRACE",
    "Č": "LBRACKET", "č": "RBRACKET", "Ċ": "COMMA", "ċ": "SEMI",
    "Ç": "ASSIGN", "ÇÇ": "EQ", "çç": "NE",
    "C̄": "PLUS", "c̄": "MINUS", "C̈": "MUL", "c̈": "DIV",
    "c̃": "FLOORDIV", "C̃": "MOD", "C̈C": "POW",
    "C̲": "LT", "c̲": "GT", "C̲C": "LE", "c̲c": "GE",
    "C̆": "TRUE", "c̆": "FALSE", "C̊": "NULL",
}

BUILTINS = {
    "C̶": "print", "c̶": "input", "C̶C": "len", "C̶c": "append",
    "C̶CC": "pop", "C̶Cc": "str", "C̶cC": "int", "C̶cc": "float",
    "C̶CCC": "range", "C̶CCc": "type", "C̶CcC": "assert",
}

IDENT_PREFIX = "ƈ"
INTEGER_PREFIX = "Ƈ"
STRING_PREFIX = "Ȼ"
_TO_BITS = str.maketrans("cC", "01")
_TO_CS = str.maketrans("01", "cC")
_LOOKALIKES = str.maketrans("Сс", "Cc")


@dataclass(frozen=True, slots=True)
class Source:
    text: str
    name: str = "<code>"


@dataclass(frozen=True, slots=True)
class Token:
    kind: str
    raw: str
    value: object
    line: int
    column: int
    start: int
    end: int
    source: Source


def normalize(raw: str) -> str:
    return unicodedata.normalize("NFC", raw.translate(_LOOKALIKES))


def tokenize(text: str, name: str = "<code>") -> list[Token]:
    if not isinstance(text, str):
        raise TypeError("Исходный код должен быть строкой.")
    if len(text) > MAX_SOURCE_CHARS:
        raise FCError("Исходник длиннее 1 000 000 символов.", category="limit")
    source = Source(text, name)
    result: list[Token] = []
    line = column = 1
    for match in re.finditer(r"[\s﻿]+|[^\s﻿]+", text):
        raw = match.group()
        if not (raw[0].isspace() or raw[0] == "﻿"):
            canonical = normalize(raw)
            token = Token("UNKNOWN", raw, canonical, line, column, match.start(), match.end(), source)
            if canonical in SYMBOLS:
                token = Token(SYMBOLS[canonical], raw, canonical, line, column, match.start(), match.end(), source)
            elif canonical in BUILTINS:
                token = Token("BUILTIN", raw, BUILTINS[canonical], line, column, match.start(), match.end(), source)
            elif re.fullmatch(r"ƈ[cC]+", canonical):
                token = Token("IDENT", raw, canonical, line, column, match.start(), match.end(), source)
            elif re.fullmatch(r"Ƈ[cC]+", canonical):
                payload = canonical[1:]
                if len(payload) > MAX_INTEGER_BITS:
                    raise FCError(f"Целочисленный литерал длиннее {MAX_INTEGER_BITS} бит.", token, "limit")
                token = Token("NUMBER", raw, int(payload.translate(_TO_BITS), 2), line, column, match.start(), match.end(), source)
            elif re.fullmatch(r"Ȼ[cC]*", canonical):
                payload = canonical[1:]
                if len(payload) % 8:
                    raise FCError("Строковый литерал должен содержать целое число 8-битных байтов.", token)
                data = bytes(int(payload[i:i + 8].translate(_TO_BITS), 2) for i in range(0, len(payload), 8))
                try:
                    value = data.decode("utf-8")
                except UnicodeDecodeError:
                    raise FCError("В строковом литерале некорректный UTF-8.", token) from None
                token = Token("STRING", raw, value, line, column, match.start(), match.end(), source)
            else:
                preview = raw if len(raw) <= 28 else raw[:28] + "…"
                raise FCError(f"Неизвестный токен {preview!r}. Все токены разделяются пробелами.", token)
            result.append(token)
        lines = re.split(r"\r\n|[\r\n  ]", raw)
        if len(lines) > 1:
            line += len(lines) - 1
            column = len(lines[-1]) + 1
        else:
            column += len(raw)
    result.append(Token("EOF", "", None, line, column, len(text), len(text), source))
    return result


def encode_int(value: int) -> str:
    if type(value) is not int:
        raise TypeError("Ожидается целое число.")
    if value.bit_length() > MAX_INTEGER_BITS:
        raise ValueError(f"Число длиннее {MAX_INTEGER_BITS} бит.")
    literal = INTEGER_PREFIX + format(abs(value), "b").translate(_TO_CS)
    return "c̄ " + literal if value < 0 else literal


def encode_string(value: str) -> str:
    data = value.encode("utf-8")
    if len(data) * 8 + 1 > MAX_SOURCE_CHARS:
        raise ValueError("Строка не помещается в максимальный размер исходника.")
    return STRING_PREFIX + "".join(format(byte, "08b").translate(_TO_CS) for byte in data)


def encode_name(value: str) -> str:
    if not value:
        raise ValueError("Имя не может быть пустым.")
    return IDENT_PREFIX + encode_string(value)[1:]


def encode_float(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("Поддерживаются только конечные float.")
    numerator, denominator = value.as_integer_ratio()
    literal = f"Ć {encode_int(numerator)} c̈ {encode_int(denominator)} ć"
    return "c̄ " + literal if value == 0 and math.copysign(1, value) < 0 else literal
