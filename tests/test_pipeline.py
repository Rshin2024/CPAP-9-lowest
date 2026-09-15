"""Pipeline regressions use temporary repositories and never push a remote."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLAN = "GEN=9 P=101 KX=100 MAXUNC=145 ITERS=10 GNUM=96 CODE=main"


class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name)
        (self.repo / "scripts").mkdir()
        (self.repo / "src").mkdir()
        (self.repo / "control").mkdir()
        for name in ("worker.sh", "collect.sh", "collect_results.py"):
            shutil.copy2(ROOT / "scripts" / name, self.repo / "scripts" / name)
        # Deliberately tiny build/verifier stubs exercise installation/control flow.
        (self.repo / "scripts/build.sh").write_text(
            '#!/bin/bash\nset -e\n[ ! -e "$1/src/FAIL" ] || exit 23\n'
            'mkdir -p "$2"\ncp "$1/src/search.c" "$2/search"\n')
        (self.repo / "scripts/verify.py").write_text("print('fixture verified')\n")
        (self.repo / "src/search.c").write_text("fixture binary v1\n")
        (self.repo / "control/plan.txt").write_text(PLAN + "\n")
        self.git("init", "-q")
        self.git("config", "user.name", "Pipeline Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("checkout", "-qb", "main")
        self.revision = self.commit()
        self.git("update-ref", "refs/remotes/origin/main", self.revision)

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.repo, check=True, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()

    def commit(self):
        self.git("add", ".")
        self.git("commit", "-qm", "fixture")
        return self.git("rev-parse", "HEAD")

    def shell(self, script, *, bash="bash"):
        env = os.environ.copy()
        env["PYTHON"] = sys.executable
        # No function under test may accidentally contact a remote.
        prelude = r'''
source scripts/worker.sh
git() {
  case "$1" in fetch|push) echo "network operation forbidden in fixture" >&2; return 99;; esac
  command git "$@"
}
ROOT=$PWD
WID=0
CODE=main
CURCODE=OLD
CURSOURCE=OLD
OUT=results/worker_0
mkdir -p "$OUT" logs
'''
        return subprocess.run([bash, "-c", prelude + script], cwd=self.repo, env=env,
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)

    def test_safe_plan_parser_and_main_alias(self):
        result = self.shell(f"parse_plan '{PLAN}\n\n# comment' && resolve_code && printf '%s\\n' \"$CODE\" \"$RESOLVED_CODE\"", bash="/bin/bash")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["main", self.revision])
        for suffix in (" EXTRA=1", " CODE=main", " P=1;touch BAD", " P=$(touch BAD)"):
            result = self.shell(f"parse_plan '{PLAN}{suffix}'")
            self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.repo / "BAD").exists())

    def test_source_identity_ignores_progress_commits(self):
        first = self.shell("resolve_code && echo \"$SOURCE_ID\"").stdout.strip()
        (self.repo / "results").mkdir(exist_ok=True)
        (self.repo / "results/progress.txt").write_text("progress only")
        newer = self.commit()
        self.git("update-ref", "refs/remotes/origin/main", newer)
        result = self.shell("resolve_code && printf '%s\\n' \"$SOURCE_ID\" \"$RESOLVED_CODE\"")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [first, newer])

    def test_success_installs_exact_snapshot_without_checkout(self):
        (self.repo / "src/search.c").write_text("uncommitted local edits\n")
        result = self.shell('''
resolve_code && update_code || exit 1
[ "$CURCODE" = "$RESOLVED_CODE" ] || exit 2
[ "$CURSOURCE" = "$SOURCE_ID" ] || exit 3
[ "$(cat "$RUN_DIR/bin/search")" = "fixture binary v1" ] || exit 4
[ "$(cat src/search.c)" = "uncommitted local edits" ] || exit 5
[ ${#BINARY_SHA256} = 64 ] || exit 6
''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_failed_build_does_not_advance_or_modify_checkout(self):
        (self.repo / "src/FAIL").touch()
        newer = self.commit()
        self.git("update-ref", "refs/remotes/origin/main", newer)
        (self.repo / "src/search.c").write_text("local edits survive\n")
        result = self.shell('''
RUN_DIR=old-release
BINARY_SHA256=old-digest
resolve_code || exit 1
if update_code; then exit 2; fi
[ "$CURCODE" = OLD ] && [ "$CURSOURCE" = OLD ] || exit 3
[ "$RUN_DIR" = old-release ] && [ "$BINARY_SHA256" = old-digest ] || exit 4
[ "$(cat src/search.c)" = "local edits survive" ] || exit 5
''')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(list((self.repo / ".releases").glob("worker_0/.build.*")))

    def test_unavailable_code_fails_without_advancing(self):
        result = self.shell('CODE=0000000000000000000000000000000000000000\nif resolve_code; then exit 2; fi\n[ "$CURCODE" = OLD ]')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_failed_install_keeps_previous_provenance(self):
        result = self.shell('''
resolve_code || exit 1
date() { if [ "$1" = +%s ]; then echo 123; else command date "$@"; fi; }
mkdir -p "$ROOT/.releases/worker_0/$RESOLVED_CODE-123-$$"
if update_code; then exit 2; fi
[ "$CURCODE" = OLD ] && [ "$CURSOURCE" = OLD ] || exit 3
''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_stop_plan_exits_before_code_resolution(self):
        stop_plan = PLAN.replace("P=101", "P=0")
        result = self.shell(f'''
read_plan() {{ printf '%s\\n' '{stop_plan}'; }}
resolve_code() {{ echo unexpected-code-resolution >&2; return 1; }}
publish() {{ echo "$RUN_STATUS"; }}
main 0 1
''', bash="/bin/bash")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "stopped")

    def test_failed_verification_propagates_and_late_hits_are_checked(self):
        (self.repo / "scripts/verify.py").write_text("raise SystemExit(7)\n")
        result = self.shell('''
RUN_DIR=$ROOT
PIDS=()
LASTHITS=0
printf 'HIT CPAP-9 N=123\n' > "$OUT/hits.txt"
publish() { echo "$RUN_STATUS $VERIFICATION_STATUS"; }
if finish 0; then exit 2; fi
[[ $VERIFICATION_STATUS == "failed exit=7"* ]] || exit 3
[ "$RUN_STATUS" = verification_failed ] || exit 4
''')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("verification_failed failed exit=7", result.stdout)

    def test_partition_range_and_remap_process_count(self):
        result = self.shell(f'''
parse_plan '{PLAN}' || exit 1
NP=4; WID=24; RUN_DIR=$ROOT
if start_procs; then exit 2; fi
WID=0; NP=2
printf '# REMAP=fixture:0\n' > src/bases_101.txt
if start_procs; then exit 3; fi
''')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("exceeds GNUM", result.stderr)
        self.assertIn("require CPUS_PER_WORKER=4", result.stderr)

    def hit(self, worker, n):
        directory = self.repo / "results" / f"worker_{worker}"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "hits.txt").write_text(f"HIT CPAP-9 k=1 digits={len(str(n))} N={n} x=0\n")
        (directory / "summary.txt").write_text(f"worker={worker} fixture\n")

    def collect(self):
        return subprocess.run([sys.executable, "scripts/collect_results.py", "--no-fetch"],
                              cwd=self.repo, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=15)

    def test_collector_enumerates_all_workers_and_local_hits_with_provenance(self):
        for worker, n in ((1, 101), (9, 103), (17, 107), (19, 109)):
            self.hit(worker, n)
        revision = self.commit()
        self.git("update-ref", "refs/remotes/origin/main", revision)
        self.git("update-ref", "refs/remotes/origin/non-worker-name", revision)
        self.hit(99, 113)
        result = self.collect()
        self.assertEqual(result.returncode, 0, result.stderr)
        records = json.loads((self.repo / "results/collected/all_hits.json").read_text())
        self.assertEqual([r["N"] for r in records], ["101", "103", "107", "109", "113"])
        self.assertEqual(len(records[0]["provenance"]), 3)
        self.assertEqual({p["commit"] for p in records[0]["provenance"] if p["kind"] == "git"}, {revision})
        self.assertEqual(records[-1]["provenance"][0]["kind"], "working_tree")
        self.assertEqual(len((self.repo / "results/collected/all_hits.txt").read_text().splitlines()), 5)

    def test_collector_rejects_malformed_and_ambiguous_hits(self):
        self.hit(1, 101)
        path = self.repo / "results/worker_1/hits.txt"
        for line in ("HIT CPAP-9 N=corrupt", "HIT CPAP-9 N=101 N=103", "HIT CPAP-9 N=101 HIT CPAP-9 N=103"):
            path.write_text(line + "\n")
            result = self.collect()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("malformed HIT", result.stderr)


if __name__ == "__main__":
    unittest.main()
