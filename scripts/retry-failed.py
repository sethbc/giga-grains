#!/usr/bin/env python3
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
"""Re-runs failed test cases once, for CI's Rosetta step.

    retry-failed.py catch2 --report XML --status N [--max 5] [--summary FILE] -- CMD...
    retry-failed.py list --failures FILE --label TEXT [--max 5] [--summary FILE] -- CMD...

catch2: N is the exit status of a Catch2 run whose XML reporter wrote XML. Only a clean "these
cases failed" qualifies for a retry: status 42 and a complete report with at least one failed
case. Each failed case is re-run as `CMD... <escaped name>`.

list: FILE has one failure per line (a missing or empty file means none failed); each is re-run
as `CMD... <line>`, the line one final argument.

Exit status: 0 when nothing failed or every retry passed, 1 otherwise, 2 for bad usage. GitHub
workflow commands go to stdout; with --summary a Markdown table is appended to FILE.
"""

import argparse
import subprocess
import sys
import xml.etree.ElementTree as ET

TITLE = "Rosetta retry"
CATCH2_FAILED = 42
FAILURE_TAGS = ("Failure", "Exception", "FatalErrorCondition")


def escape_name(name: str) -> str:
    """Catch2's test spec: a backslash before each of \\ , [ ] * ~ and "."""
    return "".join("\\" + c if c in '\\,[]*~"' else c for c in name)


def annotation(level: str, message: str) -> None:
    message = message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print(f"::{level} title={TITLE}::{message}", flush=True)


def cell(text: str) -> str:
    return text.replace("|", "\\|").replace("\r", " ").replace("\n", " ")


def no_retry(reason: str) -> int:
    annotation("error", f"no retry: {reason}")
    return 1


def first_failure(case: ET.Element) -> str:
    """The first failing Expression's `file:line expanded`, else the first Failure, Exception
    or FatalErrorCondition's, else "(no message)". Nested Sections are searched."""
    for want in (("Expression",), FAILURE_TAGS):
        for e in case.iter():
            if e.tag not in want:
                continue
            if e.tag == "Expression":
                if e.get("success") != "false":
                    continue
                expanded = e.find("Expanded")
                text = expanded.text if expanded is not None else None
            else:
                text = e.text
            where = f"{e.get('filename', '?')}:{e.get('line', '?')}"
            return f"{where} {(text or '').strip()}".strip()
    return "(no message)"


def failed_cases(report: str):
    """[(name, first failure)] for a complete report; raises ValueError otherwise."""
    try:
        root = ET.parse(report).getroot()
    except (ET.ParseError, OSError) as e:
        raise ValueError(f"the report is unreadable ({e})")
    if root.tag != "Catch2TestRun" or root.find("OverallResults") is None:
        raise ValueError("the report is incomplete")
    failed = []
    for case in root.iter("TestCase"):
        result = case.find("OverallResult")
        if result is not None and result.get("success") == "false":
            failed.append((case.get("name", ""), first_failure(case)))
    return failed


def retry(failures, command, max_retries, summary):
    """Re-runs each (name, first failure, argument) once; returns the exit status."""
    if len(failures) > max_retries:
        return no_retry(f"{len(failures)} cases failed, more than {max_retries}")
    rows = []
    status = 0
    for name, first, arg in failures:
        sys.stdout.flush()
        code = subprocess.call(command + [arg])
        sys.stdout.flush()
        if code == 0:
            annotation("warning", f"{name} failed once, passed on retry")
        else:
            annotation("error", f"{name} failed twice")
            status = 1
        rows.append((name, first, "passed" if code == 0 else "failed"))
    if summary and rows:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("\n| Case | First failure | Retry |\n| --- | --- | --- |\n")
            for row in rows:
                f.write("| " + " | ".join(cell(c) for c in row) + " |\n")
    return status


def run_catch2(args, command) -> int:
    if args.status not in (0, CATCH2_FAILED):
        return no_retry(f"exit status {args.status}")
    try:
        failed = failed_cases(args.report)
    except ValueError as e:
        return no_retry(str(e))
    if args.status == 0 and not failed:
        return 0
    if args.status == 0:
        return no_retry("exit status 0, but the report has failed cases")
    if not failed:
        return no_retry("exit status 42, but the report has no failed case")
    return retry([(n, f, escape_name(n)) for n, f in failed], command, args.max, args.summary)


def run_list(args, command) -> int:
    try:
        with open(args.failures, encoding="utf-8") as f:
            lines = [ln.rstrip("\r\n") for ln in f]
    except FileNotFoundError:
        return 0
    lines = [ln for ln in lines if ln]
    return retry([(ln, args.label, ln) for ln in lines], command, args.max, args.summary)


def parse(argv):
    if "--" not in argv:
        sys.exit(usage_error("the command after -- is missing"))
    split = argv.index("--")
    command = argv[split + 1 :]
    if not command:
        sys.exit(usage_error("the command after -- is empty"))
    parser = argparse.ArgumentParser(prog="retry-failed.py")
    sub = parser.add_subparsers(dest="mode", required=True)
    c = sub.add_parser("catch2")
    c.add_argument("--report", required=True)
    c.add_argument("--status", required=True, type=int)
    l = sub.add_parser("list")
    l.add_argument("--failures", required=True)
    l.add_argument("--label", required=True)
    for p in (c, l):
        p.add_argument("--max", type=int, default=5)
        p.add_argument("--summary")
    return parser.parse_args(argv[:split]), command


def usage_error(message: str) -> int:
    print(f"retry-failed.py: {message}", file=sys.stderr)
    return 2


def main(argv) -> int:
    args, command = parse(argv)
    return run_catch2(args, command) if args.mode == "catch2" else run_list(args, command)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
