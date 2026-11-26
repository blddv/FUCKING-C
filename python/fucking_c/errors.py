from __future__ import annotations

import unicodedata
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .lexer import Token


class FCError(Exception):
    def __init__(self, message: str, token: Token | None = None, category: str = "syntax"):
        super().__init__(message)
        self.message = message
        self.token = token
        self.category = category
        self.trace: list[tuple[str, Token]] = []

    def add_frame(self, name: str, token: Token) -> None:
        if len(self.trace) < 12:
            self.trace.append((name, token))

    def diagnostic(self) -> str:
        labels = {"syntax": "ошибка синтаксиса", "runtime": "ошибка выполнения", "limit": "лимит"}
        label = labels.get(self.category, self.category)
        if self.token is None:
            return f"Fucking C: {label}: {self.message}"
        token = self.token
        result = [f"{token.source.name}:{token.line}:{token.column}: {label}: {self.message}"]
        lines = token.source.text.splitlines()
        if 1 <= token.line <= len(lines):
            line = lines[token.line - 1]
            prefix = line[:token.column - 1].expandtabs(4)
            width = sum(0 if unicodedata.combining(char) else 2 if unicodedata.east_asian_width(char) in "WF" else 1 for char in prefix)
            result.extend((f"  {line.expandtabs(4)}", "  " + " " * width + "^"))
        for name, call in self.trace:
            result.append(f"  вызов {name}: {call.source.name}:{call.line}:{call.column}")
        return "\n".join(result)
