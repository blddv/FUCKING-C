import math
import unittest
import unicodedata

from fucking_c import FCError, capture, encode_float, encode_int, encode_name, encode_string, parse, tokenize
from fucking_c.lexer import BUILTINS, SYMBOLS, MAX_INTEGER_BITS
from fucking_c import parser as ast


class LexerTests(unittest.TestCase):
    def test_all_symbols_are_distinct_and_normalized(self):
        self.assertFalse(set(SYMBOLS) & set(BUILTINS))
        for symbol, kind in SYMBOLS.items():
            with self.subTest(symbol=symbol):
                self.assertEqual(unicodedata.normalize("NFC", symbol), symbol)
                self.assertEqual(tokenize(symbol)[0].kind, kind)

    def test_whitespace_and_locations(self):
        tokens = tokenize("C\tƈC\r\n  Ç ƇC\rċ")
        self.assertEqual([(t.line, t.column) for t in tokens[:-1]], [(1, 1), (1, 3), (2, 3), (2, 5), (3, 1)])
        self.assertEqual(tokens[-1].kind, "EOF")

    def test_bom_and_unicode_line_separators(self):
        tokens = tokenize("﻿C ƈC Ç ƇC ċ")
        self.assertEqual(tokens[0].kind, "LET")
        self.assertEqual([token.line for token in tokens[:3]], [1, 2, 3])

    def test_decomposed_and_cyrillic_variants(self):
        source = "С ƈС Ç ƇСС с̇ С̶ С́ ƈС с́ с̇"
        output, _ = capture(source)
        self.assertEqual(output, "3\n")

    def test_unknown_characters_are_errors_not_comments(self):
        for source in ("let", "123", "(", "# comment", "// comment", "CƈC", "Ƈ", "ƈ", "©", "CCCC"):
            with self.subTest(source=source), self.assertRaises(FCError):
                tokenize(source)

    def test_integer_encoding_roundtrip(self):
        for number in (0, 1, 2, 42, -42, 2**128, -(2**200)):
            with self.subTest(number=number):
                _, runtime = capture(encode_int(number) + " ċ")
                self.assertEqual(runtime.last_value, number)
        self.assertEqual(encode_int(42), "ƇCcCcCc")

    def test_string_encoding_roundtrip(self):
        for value in ("", "C", "Hello, C!", "Привет 🦊", "汉字", "\0\t\r\n", '"\\'):
            with self.subTest(value=value):
                token = tokenize(encode_string(value))[0]
                self.assertEqual(token.kind, "STRING")
                self.assertEqual(token.value, value)
        self.assertEqual(encode_string(""), "Ȼ")

    def test_strings_require_whole_valid_utf8_bytes(self):
        for value in ("ȻC", "ȻCCCCCCCC", "ȻCccccccc"):
            with self.subTest(value=value), self.assertRaises(FCError):
                tokenize(value)

    def test_encoded_names_are_identifiers(self):
        for value in ("counter", "имя", "C", "a_b"):
            self.assertEqual(tokenize(encode_name(value))[0].kind, "IDENT")
        with self.assertRaises(ValueError):
            encode_name("")

    def test_float_encoder_is_exact(self):
        for number in (0.0, -0.0, 0.1, -2.5, 1e308, 5e-324):
            with self.subTest(number=number):
                _, runtime = capture(encode_float(number) + " ċ")
                self.assertIsInstance(runtime.last_value, float)
                self.assertEqual(runtime.last_value, number)
                self.assertEqual(math.copysign(1, runtime.last_value), math.copysign(1, number))
        for number in (math.nan, math.inf, -math.inf):
            with self.assertRaises(ValueError):
                encode_float(number)

    def test_resource_limits_during_lexing(self):
        with self.assertRaises(FCError):
            tokenize(" " * 1_000_001)
        with self.assertRaises(FCError):
            tokenize("Ƈ" + "C" * (MAX_INTEGER_BITS + 1))
        with self.assertRaises(ValueError):
            encode_int(1 << MAX_INTEGER_BITS)
        with self.assertRaises(TypeError):
            encode_int(True)

    def test_diagnostic_has_filename_line_and_caret(self):
        try:
            parse("C ƈC Ç ƇC ċ\n  ???", "bad.fc")
        except FCError as error:
            diagnostic = error.diagnostic()
            self.assertIn("bad.fc:2:3", diagnostic)
            self.assertIn("^", diagnostic)
        else:
            self.fail("Expected a syntax error")


class ParserTests(unittest.TestCase):
    def test_creates_actual_ast(self):
        program = parse("C ƈC Ç ƇC C̄ ƇCc C̈ ƇCC ċ")
        node = program.statements[0]
        self.assertIsInstance(node, ast.Let)
        self.assertIsInstance(node.value, ast.Binary)
        self.assertEqual(node.value.op, "PLUS")
        self.assertEqual(node.value.right.op, "MUL")

    def test_nested_function_and_list_ast(self):
        program = parse("c ƈC Ć ƈc ć Ĉ cC Č ƈc č ċ ĉ")
        self.assertIsInstance(program.statements[0], ast.FunctionDeclaration)
        self.assertIsInstance(program.statements[0].function.body.statements[0], ast.Return)

    def test_empty_program(self):
        self.assertEqual(parse(" \n").statements, [])

    def test_semicolons_and_balanced_delimiters(self):
        invalid = ["C ƈC Ç ƇC", "Ĉ", "C̶ Ć ƇC ċ", "Č ƇC ċ", "ĉ", "Ć ć ċ", "CC ƇC Ĉ ĉ"]
        for source in invalid:
            with self.subTest(source=source), self.assertRaises(FCError):
                parse(source)

    def test_return_break_continue_context(self):
        invalid = ["cC ƇC ċ", "CcC ċ", "Ccc ċ", "Cc Ć C̆ ć Ĉ c ƈC Ć ć Ĉ CcC ċ ĉ ĉ"]
        for source in invalid:
            with self.subTest(source=source), self.assertRaises(FCError):
                parse(source)

    def test_duplicate_parameters_and_invalid_assignment(self):
        for source in ("c ƈC Ć ƈc Ċ ƈc ć Ĉ ĉ", "ƇC Ç ƇCc ċ", "Ć ƇC C̄ ƇC ć Ç ƇCc ċ"):
            with self.subTest(source=source), self.assertRaises(FCError):
                parse(source)

    def test_trailing_commas(self):
        parse("c ƈC Ć ƈc Ċ ć Ĉ cC ƈc ċ ĉ C̶ Ć ƈC Ć Č ƇC Ċ č Ċ ć Ċ ć ċ")

    def test_deep_syntax_is_language_error(self):
        with self.assertRaises(FCError) as raised:
            parse("Ć " * 2000 + "ƇC " + "ć " * 2000 + "ċ")
        self.assertEqual(raised.exception.category, "limit")


if __name__ == "__main__":
    unittest.main()
