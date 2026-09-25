import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GUARD_SCRIPT = REPOSITORY_ROOT / "scripts" / "check_architecture_guards.py"


class ArchitectureGuardsTest(unittest.TestCase):
    def runGuard(self, files: dict[str, str]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relativePath, content in files.items():
                path = root / relativePath
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")

            return subprocess.run(
                [sys.executable, str(GUARD_SCRIPT), str(root)],
                check=False,
                capture_output=True,
                text=True,
            )

    def testAllowsOperatingSystemClocksInsideTheClockAdapter(self):
        result = self.runGuard(
            {
                "src/core/time.cpp":
                    "auto a = std::chrono::system_clock::now();\n"
                    "auto b = std::chrono::steady_clock::now();\n",
            }
        )

        self.assertEqual(result.returncode, 0, result.stdout)

    def testRejectsDirectClockAccessFromProductionCode(self):
        result = self.runGuard(
            {
                "src/book/order_book.cpp":
                    "void apply() { auto t = std::chrono::system_clock::now(); }\n",
            }
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("src/book/order_book.cpp:1", result.stdout)
        self.assertIn("bypasses the injected Clock", result.stdout)

    def testCommentsAndStringLiteralsDoNotTriggerClockGuard(self):
        result = self.runGuard(
            {
                "src/book/order_book.cpp":
                    "// Never call std::chrono::system_clock::now() here.\n"
                    'const char* explanation = "std::chrono::steady_clock::now()";\n'
                    "/* std::chrono::system_clock::now() */\n",
            }
        )

        self.assertEqual(result.returncode, 0, result.stdout)

    def testRejectsFloatingPointInExactFinancialHeader(self):
        result = self.runGuard(
            {
                "include/te/core/types.hpp": "struct Price { double value; };\n",
            }
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("include/te/core/types.hpp:1", result.stdout)
        self.assertIn("floating-point type", result.stdout)

    def testRejectsFloatingPointInFinancialImplementations(self):
        for path in ("src/engine/portfolio.cpp", "src/engine/risk.cpp", "src/book/price_level.cpp"):
            with self.subTest(path=path):
                result = self.runGuard({path: "double amount;\n"})
                self.assertEqual(result.returncode, 1, result.stdout)

    def testAllowsFloatingPointOutsideExactFinancialState(self):
        result = self.runGuard(
            {
                "src/research/statistics.cpp": "double probability() { return 0.5; }\n",
                "include/te/engine/portfolio.hpp": "// Never store cash as double.\n",
            }
        )

        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
