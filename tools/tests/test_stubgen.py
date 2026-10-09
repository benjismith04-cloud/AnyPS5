from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "stubgen.py"
DEBT = ROOT / "docs" / "dev" / "TechnicalDebt.md"


class StubgenTests(unittest.TestCase):
    def generate(self, module, *options):
        with tempfile.TemporaryDirectory(prefix="stubgen links ") as directory:
            work = Path(directory)
            nids = work / "nids.txt"
            nids.write_text("AAAAAAAAAAA\nwTpfglkmv34\n", encoding="utf-8")
            database = work / "names.csv"
            database.write_text("wTpfglkmv34 sceMsgDialogProgressBarSetValue\n", encoding="utf-8")
            result = subprocess.run([sys.executable, str(SCRIPT), "--module", module,
                                     "--nids", str(nids), "--db", str(database), "--td-only", *options],
                                    cwd=work, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        targets = re.findall(r"\]\(([^)]+)\)", result.stdout)
        self.assertEqual(len(targets), 2, result.stdout)
        self.assertIn("unknown name, signature", result.stdout)
        self.assertIn("sceMsgDialogProgressBarSetValue", result.stdout)
        return targets

    def test_default_links_resolve_from_technical_debt_outside_repository(self):
        for module, filename in (("libSceAudioIn", "Export.cpp"),
                                 ("libkernel", "Unimplemented.cpp"),
                                 ("libSceFont", "src/Unimplemented.cpp")):
            with self.subTest(module=module):
                expected = ROOT / "core" / "libs" / "prx" / module / filename
                self.assertTrue(expected.is_file(), expected)
                for target in self.generate(module):
                    resolved = (DEBT.parent / target).resolve()
                    self.assertEqual(resolved, expected)
                    self.assertTrue(resolved.is_file(), resolved)

    def test_explicit_target_is_preserved(self):
        for target in ("../../custom/Export.cpp", "core/libs/prx/custom.cpp", "https://example.com/source.cpp"):
            with self.subTest(target=target):
                self.assertEqual(self.generate("libSceAudioIn", "--td-path", target), [target, target])

    def test_unknown_module_keeps_export_fallback(self):
        module = "libSceStubgenMissing"
        source = ROOT / "core" / "libs" / "prx" / module / "Export.cpp"
        self.assertFalse(source.parent.exists(), source)
        for target in self.generate(module):
            self.assertEqual((DEBT.parent / target).resolve(), source)


if __name__ == "__main__":
    unittest.main()
