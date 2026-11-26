from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from fucking_c import capture, encode_string

ROOT = Path(__file__).resolve().parents[1]


def cli(*arguments, input_text=""):
    return subprocess.run([sys.executable, str(ROOT / "fc.py"), *arguments], input=input_text,
                          text=True, encoding="utf-8", capture_output=True, timeout=10, cwd=ROOT)


class CLITests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def fixture(self, source, name="test.fc"):
        path = Path(self.temp.name) / name
        path.write_text(source, encoding="utf-8")
        return str(path)

    def test_help_version_and_syntax(self):
        self.assertEqual(cli().returncode, 0)
        self.assertIn("Fucking C", cli("--help").stdout)
        self.assertEqual(cli("--version").stdout, "Fucking C 2.0.0\n")
        self.assertIn("Ƈ", cli("syntax").stdout)

    def test_run_and_shortcut(self):
        for arguments in (("run", "examples/factorial.fc"), ("examples/factorial.fc",)):
            result = cli(*arguments)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "720\n")
            self.assertEqual(result.stderr, "")

    def test_unicode_stdin_and_explicit_input(self):
        for result in (cli("examples/echo.fc", input_text="Привет 🦊\n"), cli("examples/echo.fc", "--input", "Привет 🦊\n", input_text="ignored")):
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "Привет 🦊\n")

    def test_check_does_not_execute(self):
        path = self.fixture("Cc Ć C̆ ć Ĉ ĉ")
        result = cli("check", path)
        self.assertEqual(result.returncode, 0)
        self.assertIn("OK:", result.stdout)

    def test_tokens_have_locations(self):
        path = self.fixture("C ƈC Ç ƇCC ċ")
        result = cli("tokens", path)
        self.assertEqual(result.returncode, 0)
        self.assertIn("1:1\tLET", result.stdout)
        self.assertIn("NUMBER", result.stdout)

    def test_literal_helpers_emit_real_source(self):
        cases = [("int", "-42", -42), ("float", "0.1", .1), ("str", "Привет!", "Привет!")]
        for kind, argument, expected in cases:
            result = cli("literal", kind, argument)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(capture(result.stdout + " ċ")[1].last_value, expected)
        self.assertTrue(cli("lit", "name", "counter").stdout.startswith("ƈ"))
        self.assertEqual(cli("literal", "float", "inf").returncode, 2)

    def test_stats_do_not_pollute_program_stdout(self):
        result = cli("examples/factorial.fc", "--stats")
        self.assertEqual(result.stdout, "720\n")
        self.assertIn("Шагов:", result.stderr)

    def test_syntax_errors_have_locations_and_exit_2(self):
        path = self.fixture("C ƈC Ç ƇC ċ\n  ???")
        result = cli(path)
        self.assertEqual(result.returncode, 2)
        self.assertIn(":2:3:", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_runtime_error_and_call_trace(self):
        path = self.fixture("c ƈC Ć ć Ĉ cC ƇC c̈ Ƈc ċ ĉ ƈC Ć ć ċ")
        result = cli(path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("вызов ƈC", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_limits_preserve_previous_output(self):
        path = self.fixture("C̶ Ć ƇC ć ċ Cc Ć C̆ ć Ĉ ĉ")
        result = cli(path, "--max-steps", "50", "--stats")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "1\n")
        self.assertIn("50", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_input_file_errors_and_bom(self):
        self.assertEqual(cli(str(Path(self.temp.name) / "missing.fc")).returncode, 1)
        path = self.fixture("﻿C̶ Ć ƇC ć ċ")
        self.assertEqual(cli(path).stdout, "1\n")
        binary = Path(self.temp.name) / "bad-encoding.fc"
        binary.write_bytes(bytes([255]))
        result = cli(str(binary))
        self.assertEqual(result.returncode, 1)
        self.assertNotIn("Traceback", result.stderr)

    def test_argument_errors(self):
        for arguments in (("run",), ("check",), ("literal", "int"), ("run", "examples/factorial.fc", "--max-steps", "0"), ("run", "examples/factorial.fc", "--unknown")):
            with self.subTest(arguments=arguments):
                self.assertEqual(cli(*arguments).returncode, 2)

    def test_repl_persists_variables_and_supports_multiline_functions(self):
        source = "C ƈC Ç ƇCc ċ\nƈC C̄ ƇCC\nc ƈc Ć ƈCC ć Ĉ\ncC ƈCC C̈ ƈCC ċ\nĉ\nƈc Ć ƈC ć\n.quit\n"
        result = cli("repl", input_text=source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "5\n4\n")

    def test_repl_recovers_after_an_error(self):
        result = cli("repl", input_text="ƇC c̈ Ƈc\nƇCc C̄ ƇCC\n.quit\n")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "5\n")
        self.assertIn("Деление на ноль", result.stderr)

    def test_repl_unfinished_block_at_eof(self):
        result = cli("repl", input_text="c ƈC Ć ć Ĉ\n")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Незакрытый блок", result.stderr)

    def test_python_module_entrypoint(self):
        result = subprocess.run([sys.executable, "-m", "fucking_c", "examples/factorial.fc"],
                                capture_output=True, text=True, cwd=ROOT, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "720\n")


if __name__ == "__main__":
    unittest.main()
