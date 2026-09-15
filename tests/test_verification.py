"""Verifier and report regressions; integration smoke tests require GP/PARI."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))
import verify
import report_hit

N = "502811815791820948505989265164219187224962352997"
HIT = ("HIT CPAP-9 k=20963748 digits=48 N=" + N
       + " x=19506961754250869267574127632655931757917")
BAD_HIT = "HIT CPAP-9 k=1 digits=48 N=corrupt x=0"


class VerificationTests(unittest.TestCase):
    def run_script(self, script, *args, cwd=None, env=None):
        return subprocess.run(
            [sys.executable, str(SCRIPTS / script), *map(str, args)],
            cwd=cwd or ROOT, env=env, capture_output=True, text=True, timeout=120,
        )

    def test_pari_requires_clean_answer_and_success(self):
        failures = [(1, "1\n", ""), (0, "1\n", "*** error\n"),
                    (0, "", ""), (0, "maybe\n", ""), (0, "1\n0\n", "")]
        for status, stdout, stderr in failures:
            with self.subTest(status=status, stdout=stdout, stderr=stderr):
                result = subprocess.CompletedProcess(["gp"], status, stdout, stderr)
                with patch.object(verify.subprocess, "run", return_value=result):
                    with self.assertRaises(verify.VerificationError):
                        verify.pari_isprime(17)
        for answer, expected in [("1\n", True), ("0\n", False)]:
            result = subprocess.CompletedProcess(["gp"], 0, answer, "")
            with patch.object(verify.subprocess, "run", return_value=result):
                self.assertIs(verify.pari_isprime(17), expected)

    def test_pari_missing_and_timeout_are_errors(self):
        for error in [FileNotFoundError("gp"), subprocess.TimeoutExpired("gp", 600)]:
            with self.subTest(error=type(error).__name__):
                with patch.object(verify.subprocess, "run", side_effect=error):
                    with self.assertRaises(verify.VerificationError):
                        verify.pari_isprime(17)

    def test_missing_gp_never_verifies_real_hit(self):
        with tempfile.TemporaryDirectory() as empty:
            env = dict(os.environ, PATH=empty)
            result = self.run_script("verify.py", N, 9, env=env)
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("VERIFIED", result.stdout)
        self.assertIn("cannot run PARI/GP", result.stderr)

    def test_nonproof_never_counts_as_verified(self):
        targets = {int(N) + 210 * i for i in range(9)}
        with patch.object(verify, "is_prime", side_effect=lambda n, reps: int(n) in targets):
            for proof in [False, None]:
                with self.subTest(proof=proof):
                    with patch.object(verify, "pari_isprime", return_value=proof):
                        self.assertFalse(verify.verify(N, 9)[0])

    def test_invalid_candidate_has_nonzero_status(self):
        result = self.run_script("verify.py", 4, 2)
        self.assertEqual(result.returncode, 1)
        self.assertIn("INVALID CPAP-2 N=4", result.stdout)

    def test_input_bounds(self):
        for T in [-1, 0, 1, 11, "invalid"]:
            with self.subTest(T=T):
                result = self.run_script("verify.py", 4, T)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn("VERIFIED", result.stdout)
        for value in [-5, 0, 1, 2.5, True, "3.5"]:
            with self.subTest(N=value):
                with self.assertRaises(ValueError):
                    verify.verify(value, 2)
        self.assertEqual(self.run_script("verify.py").returncode, 2)

    def test_malformed_and_mixed_files_fail_before_success_output(self):
        for contents in [BAD_HIT, HIT + "\n" + BAD_HIT, "STAT hits=0\n"]:
            with self.subTest(contents=contents):
                with tempfile.TemporaryDirectory() as directory:
                    source = Path(directory) / "hits.txt"
                    source.write_text(contents)
                    result = self.run_script("verify.py", source)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn("VERIFIED", result.stdout)
                self.assertIn("ERROR:", result.stderr)

    def test_hit_parser_rejects_ambiguous_or_truncated_numbers(self):
        for line in ["HIT CPAP-9 N=17x", "HIT CPAP-9 N=17 N=19",
                     "HIT CPAP-0 N=17", "HIT CPAP-9 N=17 HIT CPAP-9 N=19"]:
            with self.subTest(line=line):
                with self.assertRaises(ValueError):
                    verify.parse_hit_line(line)
        self.assertEqual(verify.parse_hit_line("origin/main " + HIT), (N, 9))
        self.assertIsNone(verify.parse_hit_line("STAT hits=0"))

    def test_report_rejects_bad_metadata_before_verification(self):
        for line in [HIT.replace("digits=48", "digits=1"),
                     HIT.replace("k=20963748", "k=0"),
                     HIT.replace("k=20963748", "k=20963749"),
                     HIT.replace("x=19506961754250869267574127632655931757917", "x=" + N)]:
            with self.subTest(line=line):
                result = self.run_script("report_hit.py", line)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")

    def test_report_exposes_verifier_failure_without_prime_report(self):
        with tempfile.TemporaryDirectory() as empty:
            result = self.run_script("report_hit.py", HIT, cwd=empty,
                                     env=dict(os.environ, PATH=empty))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("cannot run PARI/GP", result.stderr)
        failed = subprocess.CompletedProcess(["verify.py"], 2, "", "proof failed")
        with patch.object(report_hit.subprocess, "run", return_value=failed):
            with self.assertRaisesRegex(verify.VerificationError, "proof failed"):
                report_hit.format_report(report_hit.parse_record(HIT))

    def test_report_rejects_invalid_candidate_and_mixed_file(self):
        result = self.run_script("report_hit.py", "HIT CPAP-2 k=1 digits=1 N=4 x=0")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("INVALID", result.stderr)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "hits.txt"
            source.write_text(HIT + "\n" + BAD_HIT)
            result = self.run_script("report_hit.py", source)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")

    @unittest.skipUnless(shutil.which("gp"), "PARI/GP is needed for the integration smoke test")
    def test_real_48_digit_hit_and_all_intermediates(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "hits.txt"
            source.write_text("STAT hits=1\n" + HIT + "\n")
            result = self.run_script("verify.py", source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("PARI-isprime=proved"), 9)
        self.assertIn("all 1672 intermediate numbers are composite", result.stdout)
        self.assertIn("VERIFIED CPAP-9 N=" + N, result.stdout)

    @unittest.skipUnless(shutil.which("gp"), "PARI/GP is needed for the integration smoke test")
    def test_report_works_outside_repository(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_script("report_hit.py", HIT, cwd=directory)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("## CPAP-9 with 48 digits", result.stdout)
        self.assertIn("20963748 * 103#", result.stdout)
        self.assertEqual(result.stdout.count("PARI-isprime=proved"), 9)


if __name__ == "__main__":
    unittest.main()
