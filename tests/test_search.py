"""Regression tests for accepted results, sieve boundaries and covering search."""
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RECORDS = [
    (103, 20963748, 19506961754250869267574127632655931757917, 502811815791820948505989265164219187224962352997),
    (109, 30671858, 8033781909612130219402278345722002199063419, 8579992108061480376671279341127356370444543388016759),
    (113, 19415540, 2021898423647162476708059063206739621710151203, 613726282295112104362207255269776704091414037951975803),
    (127, 52695495, 528519229954069584776935714983521773907033131723, 211544850012758541791314765508006681351419336749363108073),
    (139, 414754040, 2727504852722291449680429317621064586256381455748443249, 4153615160235987353949618628334093822171583643663260009575626849),
]

def primes(bound):
    return [p for p in range(2, bound + 1) if all(p % d for d in range(2, math.isqrt(p) + 1))]

class SearchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.work.name)
        cls.binary = cls.directory / "bin" / "search"
        subprocess.run([str(ROOT / "scripts/build.sh"), str(ROOT), str(cls.binary.parent)], check=True, capture_output=True, text=True)
        source = cls.directory / "harness-src" / "src"
        source.mkdir(parents=True)
        (source / "search.c").write_text('#include "' + str(ROOT / 'tests/search_harness.c') + '"\n')
        (source / "cover.c").write_text('#include "' + str(ROOT / 'src/cover.c') + '"\n')
        cls.harness = cls.directory / "harness-bin" / "search"
        subprocess.run([str(ROOT / "scripts/build.sh"), str(source.parent), str(cls.harness.parent)], check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_search(self, *args, cwd=ROOT, env=None):
        return subprocess.run([str(self.binary), *map(str, args)], cwd=cwd, env=env, text=True, capture_output=True, timeout=30)

    def test_all_reported_hits(self):
        for p, k, x, n in RECORDS:
            with self.subTest(p=p):
                result = self.run_search('-P', p, '-x', x, '-k', k - 1, '-K', k + 2)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(re.findall(r'HIT CPAP-9 .*? N=(\d+) ', result.stdout), [str(n)])

    def test_sieve_segment_boundaries(self):
        p, k, x, _ = RECORDS[0]
        modulus = math.prod(primes(p))
        qs = [q for q in primes(257) if modulus % q]
        for lo, length in [(k, 1), (k - 248, 511), (k - 248, 512), (k - 248, 513), (k - 748, 1539), (k + 1, 1024)]:
            with self.subTest(length=length):
                result = self.run_search('-P', p, '-x', x, '-k', lo, '-K', lo + length, '-L', 512, '-B', 257)
                self.assertEqual(result.returncode, 0, result.stderr)
                actual = int(re.search(r'STAT .*? surv=(\d+)', result.stdout)[1])
                expected = sum(all((i * modulus + x + 210 * t) % q for q in qs for t in range(9)) for i in range(lo, lo + length))
                self.assertEqual(actual, expected)

    def test_internal_regressions(self):
        result = subprocess.run([str(self.harness)], text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('repairing two-swap variant recovered', result.stdout)

    def test_invalid_arguments_fail_cleanly(self):
        for args in [('-P',), ('-T', 2), ('-T', 11), ('-P', 3), ('-P', 512), ('-P', 509), ('-L', 32), ('-L', 513), ('-B', -1), ('-B', 4294967295), ('-k', 0), ('-k', 2, '-K', 1), ('-e', 0), ('-i', 0), ('-P', 11, '-X', 11), ('-g', 0), ('-x', 'invalid'), ('-x', -1)]:
            with self.subTest(args=args):
                result = self.run_search(*args)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertTrue(result.stderr)

    def test_empty_sieve_and_last_uint64_segment(self):
        p, _, x, _ = RECORDS[0]
        result = self.run_search('-P', p, '-x', x, '-B', 2, '-L', 512, '-k', 2**64 - 2, '-K', 2**64 - 1)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('sieve primes: 0 (largest 0)', result.stdout)
        self.assertRegex(result.stdout, r'STAT .*? k=1 surv=1')

    def test_remap_from_release_environment(self):
        run = self.directory / 'release'
        (run / 'src').mkdir(parents=True, exist_ok=True)
        first = next(line for line in (ROOT / 'src/bases_101.txt').read_text().splitlines() if line.startswith('CFG'))
        (run / 'src/bases_101.txt').write_text('# SWAPS=1 QUOTA=1 MAXVAR=2 LAYERS=0 REMAP=worker-test:0\n' + first + '\n')
        env = dict(os.environ, CPAP_WORKER_BRANCH='prefix-worker-test')
        result = self.run_search('-P', 101, '-u', 145, '-k', 1, '-K', 2, '-g', 8, '-G', 4, cwd=run, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('process index 0', result.stdout)

    def test_single_k_bases_search_finds_record(self):
        p, k, x, n = RECORDS[0]
        run = self.directory / 'single-base'
        (run / 'src').mkdir(parents=True)
        residues = ' '.join(f'{q}:{-x % q}' for q in primes(p) if q >= 11)
        (run / f'src/bases_{p}.txt').write_text(
            '# SWAPS=1 QUOTA=0 MAXVAR=1 LAYERS=1\n'
            f'CFG unc=141 r={x % 210} {residues}\n')
        result = self.run_search('-P', p, '-u', 141, '-k', k, '-K', k + 1, cwd=run)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(re.findall(r'HIT CPAP-9 .*? N=(\d+) ', result.stdout), [str(n)])
        self.assertRegex(result.stdout, r'STAT .*? k=1 surv=1')

    def test_cover_output_and_limits(self):
        binary = self.binary.with_name('cover')
        result = subprocess.run([str(binary), '101', '10000', '10', '12345', '20'], text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        for line in result.stdout.splitlines():
            u = int(re.search(r'unc=(\d+)', line)[1]); r = int(re.search(r'r=(\d+)', line)[1])
            pairs = [tuple(map(int, pair)) for pair in re.findall(r'(\d+):(\d+)', line)]
            self.assertTrue(all(b not in {210 * i % p for i in range(9)} for p, b in pairs))
            actual = sum(j % 210 != 0 and math.gcd(r + j, 210) == 1 and all(j % p != b for p, b in pairs) for j in range(1, 1680))
            self.assertEqual(u, actual)
        for args in [('3',), ('509',), ('101', '0'), ('11', '1', '1', '1', '0', '11')]:
            self.assertEqual(subprocess.run([str(binary), *args], capture_output=True).returncode, 1)

if __name__ == '__main__':
    unittest.main()
