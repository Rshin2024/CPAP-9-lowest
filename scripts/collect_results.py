#!/usr/bin/env python3
"""Collect distinct numerical hits while retaining every source occurrence."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKER_FILE = re.compile(r"results/worker_[^/]+/(hits|summary)\.txt\Z")
HIT = re.compile(r"(?:^|\s)HIT\s+CPAP-([0-9]+)\s")
NUMBER = re.compile(r"(?:^|\s)N=([0-9]+)(?:\s|$)")


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True,
                          text=True, stdout=subprocess.PIPE).stdout


def sources():
    for entry in git("for-each-ref", "--format=%(refname) %(objectname) %(symref)",
                     "refs/remotes/origin/").splitlines():
        fields = entry.split()
        if len(fields) != 2:  # Skip symbolic origin/HEAD aliases.
            continue
        ref, revision = fields
        for path in git("ls-tree", "-r", "--name-only", revision, "--", "results/").splitlines():
            if WORKER_FILE.fullmatch(path):
                yield {"kind": "git", "ref": ref, "commit": revision, "path": path}, git("show", f"{revision}:{path}")
    for path in sorted((ROOT / "results").glob("worker_*/*.txt")):
        relative = path.relative_to(ROOT).as_posix()
        if path.is_file() and WORKER_FILE.fullmatch(relative):
            content = path.read_text()
            yield {"kind": "working_tree", "path": relative,
                   "sha256": hashlib.sha256(content.encode()).hexdigest()}, content


def collect():
    hits, summaries = {}, []
    for source, content in sources():
        if source["path"].endswith("/summary.txt"):
            summaries.append((source, content))
            continue
        for line_number, line in enumerate(content.splitlines(), 1):
            if not re.search(r"(?:^|\s)HIT(?:\s|$)", line):
                continue
            patterns, numbers = list(HIT.finditer(line)), list(NUMBER.finditer(line))
            if (len(patterns) != 1 or len(numbers) != 1
                    or len(re.findall(r"(?:^|\s)HIT(?:\s|$)", line)) != 1
                    or len(re.findall(r"(?:^|\s)N=", line)) != 1):
                raise ValueError(f"malformed HIT at {source['path']}:{line_number}")
            pattern, number = patterns[0], numbers[0]
            n, length = int(number[1]), int(pattern[1])
            if n < 2 or not 2 <= length <= 10:
                raise ValueError(f"invalid HIT at {source['path']}:{line_number}")
            record = hits.setdefault(n, {"N": str(n), "T": length, "line": line, "provenance": []})
            if length > record["T"]:
                record.update(T=length, line=line)
            record["provenance"].append({**source, "line_number": line_number, "line": line})
    records = [hits[n] for n in sorted(hits)]
    out = ROOT / "results" / "collected"
    out.mkdir(parents=True, exist_ok=True)
    (out / "all_hits.txt").write_text("".join(record["line"] + "\n" for record in records))
    (out / "all_hits.json").write_text(json.dumps(records, indent=2) + "\n")
    (out / "all_summaries.txt").write_text("".join(
        f"== {json.dumps(source, sort_keys=True)}\n{content.rstrip()}\n" for source, content in summaries))
    print(f"HITS: {len(records)} distinct starting primes")
    for record in records:
        print(record["line"])
    print(f"SOURCES: {len(summaries)} worker summaries; provenance in {out.relative_to(ROOT)}/all_hits.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-fetch", action="store_true", help="use existing remote refs")
    args = parser.parse_args()
    try:
        if not args.no_fetch:
            git("fetch", "-q", "origin", "+refs/heads/*:refs/remotes/origin/*")
        collect()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"collection failed: {exc}\n")


if __name__ == "__main__":
    main()
