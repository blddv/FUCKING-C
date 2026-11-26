from __future__ import annotations

import argparse
import io
from pathlib import Path
import sys
import time

from . import __version__
from .errors import FCError
from .lexer import BUILTINS, SYMBOLS, MAX_SOURCE_CHARS, encode_float, encode_int, encode_name, encode_string, tokenize
from .parser import ExpressionStatement, parse
from .runtime import Runtime, display


def positive_integer(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("требуется целое число") from None
    if number < 1:
        raise argparse.ArgumentTypeError("требуется положительное число")
    return number


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="fc", description="Fucking C — высокоуровневый язык с алфавитом из C-вариаций.")
    parser.add_argument("--version", action="version", version=f"Fucking C {__version__}")
    commands = parser.add_subparsers(dest="command", required=True)
    execute = commands.add_parser("run", help="выполнить файл .fc")
    execute.add_argument("file", type=Path)
    execute.add_argument("--input", help="входной текст вместо stdin")
    execute.add_argument("--max-steps", type=positive_integer, default=1_000_000)
    execute.add_argument("--max-depth", type=positive_integer, default=128)
    execute.add_argument("--max-output", type=positive_integer, default=1_048_576)
    execute.add_argument("--stats", action="store_true", help="статистика в stderr")
    for name, help_text in (("check", "проверить синтаксис без выполнения"), ("tokens", "показать токены и координаты")):
        command = commands.add_parser(name, help=help_text)
        command.add_argument("file", type=Path)
    commands.add_parser("repl", help="интерактивная консоль с сохранением переменных")
    commands.add_parser("syntax", help="напечатать таблицу команд")
    literal = commands.add_parser("literal", aliases=["lit"], help="закодировать число, строку или имя в C")
    literal.add_argument("kind", choices=["int", "float", "str", "name"])
    literal.add_argument("value")
    return parser


def read_source(path: Path) -> str:
    with path.open("r", encoding="utf-8-sig") as file:
        source = file.read(MAX_SOURCE_CHARS + 1)
    if len(source) > MAX_SOURCE_CHARS:
        raise FCError("Исходник длиннее 1 000 000 символов.", category="limit")
    return source


def syntax() -> None:
    print("Токены обязательно разделяются пробелами. Все исходники — UTF-8.\n")
    for token, kind in SYMBOLS.items():
        print(f"  {token:<6} {kind.lower()}")
    print("\nВстроенные функции:")
    for token, name in BUILTINS.items():
        print(f"  {token:<6} {name}")
    print("\nƈ + c/C: имя. Ƈ + биты c=0, C=1: целое число.")
    print("Ȼ + байты UTF-8, записанные c/C: строка. Пустая строка: Ȼ.")
    print('Подсказка: fc literal str "Привет" или fc literal int 42.')


def repl() -> int:
    runtime = Runtime()
    interactive = sys.stdin.isatty()
    if interactive:
        print(f"Fucking C {__version__}. .help — справка, .quit — выход.")
    lines: list[str] = []
    command_number = 1
    while True:
        try:
            if interactive:
                print("C… " if lines else "C> ", end="", flush=True)
            line = sys.stdin.readline()
            if not line:
                if lines:
                    try:
                        parse("\n".join(lines), f"<repl:{command_number}>")
                    except FCError as error:
                        print(error.diagnostic(), file=sys.stderr)
                        return 2
                return 0
            if not lines and line.strip() in {".quit", ".exit"}:
                return 0
            if not lines and line.strip() == ".help":
                syntax()
                print("Блоки можно вводить на нескольких строках. Выражения печатают результат.")
                continue
            if not lines and not line.strip():
                continue
            lines.append(line.rstrip("\n"))
            source = "\n".join(lines)
            tokens = tokenize(source, f"<repl:{command_number}>")
            balance = 0
            for token in tokens:
                if token.kind in {"LPAREN", "LBRACE", "LBRACKET"}:
                    balance += 1
                elif token.kind in {"RPAREN", "RBRACE", "RBRACKET"}:
                    balance -= 1
            if balance > 0:
                continue
            if len(tokens) > 1 and tokens[-2].kind not in {"SEMI", "RBRACE"}:
                source += " ċ"
            program = parse(source, f"<repl:{command_number}>")
            runtime.reset_budget()
            value = runtime.run(program)
            if program.statements and isinstance(program.statements[-1], ExpressionStatement) and value is not None:
                runtime.write(display(value) + "\n", program.statements[-1].token)
            lines.clear()
            command_number += 1
        except FCError as error:
            print(error.diagnostic(), file=sys.stderr)
            lines.clear()
            command_number += 1
        except KeyboardInterrupt:
            lines.clear()
            if interactive:
                print("\nC: ввод отменён")
            else:
                return 130


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    known_commands = {"run", "check", "tokens", "repl", "syntax", "literal", "lit"}
    if arguments and not arguments[0].startswith("-") and arguments[0] not in known_commands:
        arguments.insert(0, "run")
    parser = argument_parser()
    if not arguments:
        parser.print_help()
        return 0
    args = parser.parse_args(arguments)
    try:
        if args.command == "syntax":
            syntax()
            return 0
        if args.command in {"literal", "lit"}:
            encoders = {"int": lambda text: encode_int(int(text, 10)),
                        "float": lambda text: encode_float(float(text)), "str": encode_string, "name": encode_name}
            print(encoders[args.kind](args.value))
            return 0
        if args.command == "repl":
            return repl()
        source = read_source(args.file)
        if args.command == "tokens":
            for token in tokenize(source, str(args.file)):
                preview = token.raw if len(token.raw) <= 50 else token.raw[:50] + "…"
                print(f"{token.line}:{token.column}\t{token.kind:<12}\t{preview}")
            return 0
        program = parse(source, str(args.file))
        if args.command == "check":
            print(f"OK: {args.file} — синтаксис корректен.")
            return 0
        runtime = Runtime(stdin=io.StringIO(args.input) if args.input is not None else None,
                          max_steps=args.max_steps, max_depth=args.max_depth, max_output=args.max_output)
        started = time.perf_counter()
        try:
            runtime.run(program)
        finally:
            if args.stats:
                elapsed = (time.perf_counter() - started) * 1000
                print(f"Шагов: {runtime.steps}; вывод: {runtime.output_bytes} байт; время: {elapsed:.2f} мс.", file=sys.stderr)
        return 0
    except FCError as error:
        print(error.diagnostic(), file=sys.stderr)
        return 2 if error.category == "syntax" else 1
    except (OSError, UnicodeError) as error:
        print(f"Fucking C: {error}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"Fucking C: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nFucking C: выполнение прервано.", file=sys.stderr)
        return 130
