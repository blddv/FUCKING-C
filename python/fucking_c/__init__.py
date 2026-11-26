"""Fucking C: a C-only alphabet, a real high-level language, no browser."""

__version__ = "2.0.0"

from .errors import FCError
from .lexer import encode_float, encode_int, encode_name, encode_string, tokenize
from .parser import parse
from .runtime import Runtime, capture, run

__all__ = ["FCError", "Runtime", "capture", "encode_float", "encode_int", "encode_name", "encode_string", "parse", "run", "tokenize"]
