"""Independent numerical checks of the five published progressions."""
import math
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
RECORDS = (
    502811815791820948505989265164219187224962352997,
    8579992108061480376671279341127356370444543388016759,
    613726282295112104362207255269776704091414037951975803,
    211544850012758541791314765508006681351419336749363108073,
    4153615160235987353949618628334093822171583643663260009575626849,
)


class PublishedResults(unittest.TestCase):
    def test_all_five_are_consecutive_prime_progressions(self):
        text = (ROOT / "results/RESULTS.md").read_text()
        self.assertEqual(tuple(map(int, re.findall(r"^N = (\d+)$", text, re.M))), RECORDS)
        primes = [p for p in range(2, 140)
                  if all(p % d for d in range(2, math.isqrt(p) + 1))]
        for n in RECORDS:
            with self.subTest(digits=len(str(n))):
                program = f"for(i=0,8,print(isprime({n}+210*i)))\nquit\n"
                result = subprocess.run(["gp", "-q", "-f"], input=program, text=True,
                                        capture_output=True, timeout=120, check=True)
                self.assertFalse(result.stderr.strip(), result.stderr)
                self.assertEqual(result.stdout.split(), ["1"] * 9)
                for j in range(1, 1680):
                    if j % 210 == 0:
                        continue
                    v = n + j
                    if not any(v % p == 0 for p in primes):
                        self.assertNotEqual(pow(2, v - 1, v), 1, (n, j))


if __name__ == "__main__":
    unittest.main()
