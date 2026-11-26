import io
import operator
from pathlib import Path
import random
import unittest

from fucking_c import FCError, Runtime, capture, encode_float, encode_int, encode_string, parse
from fucking_c.runtime import display

ROOT = Path(__file__).resolve().parents[1]


def value(expression):
    return capture(expression + " ċ")[1].last_value


class ExpressionTests(unittest.TestCase):
    def test_arithmetic_and_precedence(self):
        cases = {
            "ƇCc C̄ ƇCC C̈ ƇCcc": 14,
            "Ć ƇCc C̄ ƇCC ć C̈ ƇCcc": 20,
            "ƇCcCc c̄ ƇCC c̄ ƇCc": 5,
            "ƇCCC c̈ ƇCc": 3.5,
            "c̄ ƇCCC c̃ ƇCc": -4,
            "c̄ ƇCCC C̃ ƇCc": 1,
            "ƇCc C̈C ƇCC C̈C ƇCc": 512,
            "c̄ ƇCc C̈C ƇCc": -4,
            "Ć c̄ ƇCc ć C̈C ƇCc": 4,
            "ƇCc C̈C c̄ ƇCc": 0.25,
        }
        for source, expected in cases.items():
            with self.subTest(source=source):
                self.assertEqual(value(source), expected)

    def test_numbers_do_not_wrap_at_255(self):
        self.assertEqual(value("ƇCCCCCCCC C̄ ƇC"), 256)
        self.assertEqual(value(encode_int(2**100) + " C̈ ƇCc"), 2**101)

    def test_comparison_chains_and_strict_bool_equality(self):
        self.assertTrue(value("ƇC C̲ ƇCc C̲C ƇCC"))
        self.assertFalse(value("ƇCc C̲ ƇC C̲ ƈC"))
        self.assertFalse(value("C̆ ÇÇ ƇC"))
        self.assertTrue(value("ƇC ÇÇ " + encode_float(1.0)))
        self.assertTrue(value("C̊ ÇÇ C̊"))
        self.assertTrue(value("ƇC çç ƇCc"))

    def test_logical_short_circuit_and_operand_values(self):
        self.assertFalse(value("c̆ cCC ƈC"))
        self.assertTrue(value("C̆ cCc ƈC"))
        self.assertEqual(value("Ƈc cCc ƇCc"), 2)
        self.assertEqual(value("ƇCc cCC ƇCC"), 3)
        self.assertTrue(value("ccC ƇC ÇÇ ƇCc"))
        self.assertFalse(value("ccC C̆ cCc c̆"))

    def test_all_six_value_types(self):
        expressions = {"ƇC": "int", encode_float(1.5): "float", "C̆": "bool", "C̊": "null", "Ȼ": "str", "Č č": "list"}
        for source, expected in expressions.items():
            self.assertEqual(value(f"C̶CCc Ć {source} ć"), expected)

    def test_unary_and_binary_type_errors(self):
        for source in ("C̆ C̄ ƇC", "ƇC C̄ C̊", "C̊ c̄ ƇC", "c̄ Ȼ", "Ȼ C̄ ƇC", "C̆ C̲ ƇCc"):
            with self.subTest(source=source), self.assertRaises(FCError):
                value(source)

    def test_zero_division_and_invalid_power(self):
        for source in ("ƇC c̈ Ƈc", "ƇC c̃ Ƈc", "ƇC C̃ Ƈc", "Ƈc C̈C c̄ ƇC", "Ć c̄ ƇC ć C̈C Ć ƇC c̈ ƇCc ć"):
            with self.subTest(source=source), self.assertRaises(FCError):
                value(source)

    def test_unicode_string_operations(self):
        hello = encode_string("Привет")
        self.assertEqual(value(f"{hello} C̄ {encode_string(' 🦊')}"), "Привет 🦊")
        self.assertEqual(value(f"{encode_string('я')} C̈ ƇCC"), "яяя")
        self.assertEqual(value(f"ƇCc C̈ {encode_string('ab')}"), "abab")
        self.assertEqual(value(f"C̶C Ć {encode_string('🦊я')} ć"), 2)
        self.assertEqual(value(f"{hello} Č c̄ ƇC č"), "т")
        self.assertEqual(value(f"{hello} Č Ƈc č"), "П")

    def test_list_literals_concatenation_and_repetition(self):
        self.assertEqual(value("Č ƇC Ċ C̆ Ċ C̊ č"), [1, True, None])
        self.assertEqual(value("Č ƇC č C̄ Č ƇCc č"), [1, 2])
        self.assertEqual(value("Č ƇC č C̈ ƇCC"), [1, 1, 1])
        self.assertEqual(value("Č ƇC č Č c̄ ƇC č"), 1)
        self.assertTrue(value("Č ƇC Ċ Č ƇCc č č ÇÇ Č ƇC Ċ Č ƇCc č č"))

    def test_index_errors(self):
        for source in ("Č č Č Ƈc č", "Ȼ Č Ƈc č", "ƇC Č Ƈc č", "Č ƇC č Č C̆ č", "Č ƇC č Č ƇCc č"):
            with self.subTest(source=source), self.assertRaises(FCError):
                value(source)

    def test_200_random_arithmetic_trees(self):
        rng = random.Random(7331)
        operators = [("C̄", operator.add), ("c̄", operator.sub), ("C̈", operator.mul)]
        def tree(depth):
            if depth == 0 or rng.random() < .3:
                number = rng.randint(-20, 20)
                return encode_int(number), number
            left, a = tree(depth - 1)
            right, b = tree(depth - 1)
            symbol, function = rng.choice(operators)
            return f"Ć {left} {symbol} {right} ć", function(a, b)
        for _ in range(200):
            source, expected = tree(4)
            self.assertEqual(value(source), expected, source)


class StatementTests(unittest.TestCase):
    def test_declaration_assignment_and_shadowing(self):
        output, _ = capture("C ƈC Ç ƇC ċ Ĉ C ƈC Ç ƇCc ċ C̶ Ć ƈC ć ċ ĉ ƈC Ç ƈC C̄ ƇCC ċ C̶ Ć ƈC ć ċ")
        self.assertEqual(output, "2\n4\n")

    def test_redeclaration_undefined_assignment_and_scope_errors(self):
        for source in ("C ƈC Ç ƇC ċ C ƈC Ç ƇCc ċ", "ƈC Ç ƇC ċ", "Ĉ C ƈC Ç ƇC ċ ĉ ƈC ċ", "C̶ Ç ƇC ċ"):
            with self.subTest(source=source), self.assertRaises(FCError):
                capture(source)

    def test_if_else_if_else(self):
        source = "CC Ć c̆ ć Ĉ C̶ Ć ƇC ć ċ ĉ cc CC Ć C̆ ć Ĉ C̶ Ć ƇCc ć ċ ĉ cc Ĉ C̶ Ć ƇCC ć ċ ĉ"
        self.assertEqual(capture(source)[0], "2\n")

    def test_while_continue_and_break(self):
        source = """
C ƈC Ç Ƈc ċ
Cc Ć C̆ ć Ĉ
    ƈC Ç ƈC C̄ ƇC ċ
    CC Ć ƈC ÇÇ ƇCc ć Ĉ Ccc ċ ĉ
    CC Ć ƈC c̲ ƇCcc ć Ĉ CcC ċ ĉ
    C̶ Ć ƈC ć ċ
ĉ
"""
        self.assertEqual(capture(source)[0], "1\n3\n4\n")

    def test_for_range_and_iteration_scope(self):
        source = "CCC Ć ƈC CCc C̶CCC Ć ƇCC ć ć Ĉ C̶ Ć ƈC ć ċ ĉ"
        self.assertEqual(capture(source)[0], "0\n1\n2\n")
        with self.assertRaises(FCError):
            capture(source + " ƈC ċ")

    def test_for_iterates_unicode_and_snapshots_lists(self):
        self.assertEqual(capture(f"CCC Ć ƈC CCc {encode_string('я🦊')} ć Ĉ C̶ Ć ƈC ć ċ ĉ")[0], "я\n🦊\n")
        source = "C ƈC Ç Č ƇC Ċ ƇCc č ċ CCC Ć ƈc CCc ƈC ć Ĉ C̶c Ć ƈC Ċ ƇCC ć ċ ĉ C̶ Ć ƈC ć ċ"
        self.assertEqual(capture(source)[0], "[1, 2, 3, 3]\n")

    def test_list_mutation_aliasing_and_nested_indices(self):
        source = "C ƈC Ç Č Č ƇC č č ċ C ƈc Ç ƈC ċ ƈc Č Ƈc č Č Ƈc č Ç ƇCC ċ C̶ Ć ƈC ć ċ"
        self.assertEqual(capture(source)[0], "[[3]]\n")
        with self.assertRaises(FCError):
            capture(f"C ƈC Ç {encode_string('a')} ċ ƈC Č Ƈc č Ç {encode_string('b')} ċ")

    def test_examples(self):
        cases = {"factorial.fc": "720\n", "fibonacci.fc": "6765\n", "map.fc": "[1, 4, 9, 16]\n", "closure.fc": "1 2 3\n", "lists.fc": "[10, 2, 3, 4]\n4\n3\n3\n", "loops.fc": "31\n"}
        for filename, expected in cases.items():
            with self.subTest(filename=filename):
                self.assertEqual(capture((ROOT / "examples" / filename).read_text())[0], expected)


class FunctionTests(unittest.TestCase):
    def test_anonymous_function_and_immediate_call(self):
        self.assertEqual(value("Ć c Ć ƈC ć Ĉ cC ƈC C̈ ƈC ċ ĉ ć Ć ƇCC ć"), 9)

    def test_function_alias_and_return_value(self):
        source = "c ƈC Ć ƈc ć Ĉ cC ƈc C̄ ƇC ċ ĉ C ƈCC Ç ƈC ċ ƈCC Ć ƇCc ć ċ"
        self.assertEqual(capture(source)[1].last_value, 3)

    def test_lexical_not_call_site_scope(self):
        source = "C ƈC Ç ƇC ċ c ƈc Ć ć Ĉ cC ƈC ċ ĉ c ƈCC Ć ƈC ć Ĉ cC ƈc Ć ć ċ ĉ ƈCC Ć ƇCCC ć ċ"
        self.assertEqual(capture(source)[1].last_value, 1)

    def test_iteration_closures_have_distinct_bindings(self):
        source = """
C ƈC Ç Č č ċ
CCC Ć ƈc CCc C̶CCC Ć ƇCC ć ć Ĉ
    C̶c Ć ƈC Ċ c Ć ć Ĉ cC ƈc ċ ĉ ć ċ
ĉ
C̶ Ć ƈC Č Ƈc č Ć ć Ċ ƈC Č ƇC č Ć ć Ċ ƈC Č ƇCc č Ć ć ć ċ
"""
        self.assertEqual(capture(source)[0], "0 1 2\n")

    def test_mutual_recursion(self):
        source = """
c ƈC Ć ƈCC ć Ĉ
    CC Ć ƈCC ÇÇ Ƈc ć Ĉ cC C̆ ċ ĉ
    cC ƈc Ć ƈCC c̄ ƇC ć ċ
ĉ
c ƈc Ć ƈCC ć Ĉ
    CC Ć ƈCC ÇÇ Ƈc ć Ĉ cC c̆ ċ ĉ
    cC ƈC Ć ƈCC c̄ ƇC ć ċ
ĉ
ƈC Ć ƇCCc ć ċ
"""
        self.assertTrue(capture(source)[1].last_value)

    def test_return_exits_nested_loops(self):
        source = "c ƈC Ć ć Ĉ Cc Ć C̆ ć Ĉ CCC Ć ƈc CCc Č ƇCC č ć Ĉ cC ƈc ċ ĉ ĉ ĉ ƈC Ć ć ċ"
        self.assertEqual(capture(source)[1].last_value, 3)

    def test_implicit_and_empty_return_are_null(self):
        for body in ("", "cC ċ"):
            self.assertIsNone(capture(f"c ƈC Ć ć Ĉ {body} ĉ ƈC Ć ć ċ")[1].last_value)

    def test_arity_and_not_callable_errors(self):
        for source in ("ƇC Ć ć ċ", "c ƈC Ć ƈc ć Ĉ ĉ ƈC Ć ć ċ", "c ƈC Ć ć Ĉ ĉ ƈC Ć ƇC ć ċ"):
            with self.subTest(source=source), self.assertRaises(FCError):
                capture(source)

    def test_call_stack_diagnostic(self):
        source = "c ƈC Ć ć Ĉ cC ƇC c̈ Ƈc ċ ĉ ƈC Ć ć ċ"
        with self.assertRaises(FCError) as caught:
            capture(source)
        self.assertIn("вызов ƈC", caught.exception.diagnostic())
        self.assertEqual(caught.exception.category, "runtime")


class BuiltinAndLimitTests(unittest.TestCase):
    def test_input_lines_eof_and_unicode_output(self):
        source = (ROOT / "examples/echo.fc").read_text()
        self.assertEqual(capture(source, "Привет!\n\n🦊\r\n")[0], "Привет!\n\n🦊\n")
        output, _ = capture(f"C̶ Ć c̶ Ć {encode_string('> ')} ć ć ċ", "hello\n")
        self.assertEqual(output, "> hello\n")
        self.assertIsNone(value("c̶ Ć ć"))

    def test_print_formatting_and_null_result(self):
        output, runtime = capture(f"C̶ Ć C̆ Ċ c̆ Ċ C̊ Ċ Č {encode_string('я')} č ć ċ")
        self.assertEqual(output, 'true false null ["я"]\n')
        self.assertIsNone(runtime.last_value)

    def test_len_append_pop_and_str(self):
        source = "C ƈC Ç Č ƇC č ċ C̶c Ć ƈC Ċ ƇCc ć ċ C̶ Ć C̶C Ć ƈC ć Ċ C̶CC Ć ƈC Ċ Ƈc ć Ċ ƈC ć ċ"
        self.assertEqual(capture(source)[0], "2 1 [2]\n")
        self.assertEqual(value("C̶Cc Ć C̆ ć"), "true")

    def test_numeric_conversions(self):
        self.assertEqual(value(f"C̶cC Ć {encode_string(' -42 ')} ć"), -42)
        self.assertEqual(value(f"C̶cC Ć {encode_float(-2.8)} ć"), -2)
        self.assertEqual(value(f"C̶cc Ć {encode_string('2.5')} ć"), 2.5)
        for text in ("nan", "inf", "not-a-number"):
            with self.assertRaises(FCError):
                value(f"C̶cc Ć {encode_string(text)} ć")

    def test_range_and_assert(self):
        self.assertEqual(value("C̶CCC Ć ƇCc ć"), [0, 1])
        self.assertEqual(value("C̶CCC Ć ƇC Ċ ƇCcC Ċ ƇCc ć"), [1, 3])
        self.assertEqual(value("C̶CCC Ć ƇCC Ċ Ƈc Ċ c̄ ƇC ć"), [3, 2, 1])
        self.assertIsNone(value("C̶CcC Ć C̆ ć"))
        with self.assertRaisesRegex(FCError, "oops"):
            value(f"C̶CcC Ć c̆ Ċ {encode_string('oops')} ć")

    def test_native_argument_validation(self):
        expressions = ["C̶C Ć ć", "C̶C Ć ƇC ć", "C̶c Ć Ȼ Ċ ƇC ć", "C̶CC Ć Č č ć", "C̶CCC Ć Ƈc Ċ ƇC Ċ Ƈc ć", "C̶CCC Ć C̆ ć", "c̶ Ć ƇC ć", "C̶cC Ć C̊ ć", f"C̶cC Ć {encode_string('2.5')} ć", "C̶cc Ć Č č ć"]
        for expression in expressions:
            with self.subTest(expression=expression), self.assertRaises(FCError):
                value(expression)

    def test_cyclic_lists_format_and_compare(self):
        source = "C ƈC Ç Č č ċ C̶c Ć ƈC Ċ ƈC ć ċ C ƈc Ç Č č ċ C̶c Ć ƈc Ċ ƈc ć ċ C̶ Ć ƈC Ċ ƈC ÇÇ ƈc ć ċ"
        self.assertEqual(capture(source)[0], "[[...]] true\n")

    def test_infinite_loop_step_limit(self):
        runtime = Runtime(stdin=io.StringIO(), stdout=io.StringIO(), max_steps=40)
        with self.assertRaises(FCError) as caught:
            runtime.run(parse("Cc Ć C̆ ć Ĉ ĉ"))
        self.assertEqual(caught.exception.category, "limit")
        self.assertEqual(runtime.steps, 40)

    def test_recursion_limit_and_unwinding(self):
        runtime = Runtime(stdin=io.StringIO(), stdout=io.StringIO(), max_depth=10)
        with self.assertRaises(FCError) as caught:
            runtime.run(parse("c ƈC Ć ć Ĉ cC ƈC Ć ć ċ ĉ ƈC Ć ć ċ"))
        self.assertEqual(caught.exception.category, "limit")
        self.assertEqual(runtime.call_depth, 0)
        self.assertIn("10", caught.exception.message)

    def test_output_limit_counts_utf8_bytes(self):
        source = f"C̶ Ć {encode_string('я')} ć ċ"
        self.assertEqual(capture(source, max_output=3)[0], "я\n")
        with self.assertRaises(FCError):
            capture(source, max_output=2)

    def test_large_values_are_bounded_before_allocation(self):
        expressions = [f"Č ƇC č C̈ {encode_int(100001)}", f"{encode_string('x')} C̈ {encode_int(1000001)}", f"C̶CCC Ć {encode_int(100001)} ć", f"ƇCc C̈C {encode_int(1000000)}"]
        for expression in expressions:
            with self.subTest(expression=expression), self.assertRaises(FCError):
                value(expression)
        self.assertEqual(value(f"Ȼ C̈ {encode_int(2**100)}"), "")
        self.assertEqual(value(f"Č č C̈ {encode_int(2**100)}"), [])

    def test_invalid_configuration(self):
        for name in ("max_steps", "max_depth", "max_output"):
            for invalid in (0, -1, True, 1.5):
                with self.assertRaises(ValueError):
                    Runtime(**{name: invalid})


if __name__ == "__main__":
    unittest.main()
