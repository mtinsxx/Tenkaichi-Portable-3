#!/usr/bin/env python3
"""Offline pre-publication audit of reachable Git blobs and commit metadata.

Does not print matched secret values. Run inside an actual full Git clone;
a ZIP download has no commit history to examine.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

PATTERNS = {
    "GitHub token": rb"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{40,})",
    "private key": rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----",
    "AWS access key": rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b",
    "known API key": rb"(?:AIza[0-9A-Za-z_-]{30,}|sk_live_[A-Za-z0-9]{20,}|glpat-[A-Za-z0-9_-]{18,})",
    "webhook": rb"https://(?:discord(?:app)?\.com/api/webhooks|hooks\.slack\.com/services)/[^\s\"'<>]+",
    "hardcoded credential": rb"\b(?:api_?key|client_?secret|auth_?token|password)\b\s*[:=]\s*[\"'][A-Za-z0-9+/_=-]{20,}[\"']",
}
COMPILED = {name: re.compile(pat, re.I if name == "hardcoded credential" else 0)
            for name, pat in PATTERNS.items()}
PERSONAL_EMAIL_DOMAIN = re.compile(r"^(gmail|hotmail|outlook|yahoo|proton|icloud|live)\.", re.I)
RISKY_FILENAME = re.compile(r"(?:^|/)(?:\.env(?:\.[^/]*)?|id_rsa|id_ed25519|credentials(?:\.[^/]*)?|secrets?\.(?:json|ya?ml)|[^/]+\.(?:pem|p12|pfx|jks|keystore|iso|rvz|afs|pss|pbp))$", re.I)


def git(root: pathlib.Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(root), *args], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def main() -> int:
    try:
        root = pathlib.Path(git(pathlib.Path.cwd(), "rev-parse", "--show-toplevel").decode().strip())
        git(root, "rev-parse", "--verify", "HEAD")
        objects = git(root, "rev-list", "--objects", "--all").decode("utf-8", "replace").splitlines()
        commits = git(root, "log", "--all", "--format=%H%x09%ae%x09%ce").decode("utf-8", "replace").splitlines()
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        print("ERROR: full Git checkout required; ZIP files cannot scan the history.", file=sys.stderr)
        print(type(exc).__name__, file=sys.stderr)
        return 2

    personal_emails = set()
    commits_exposing_personal_email = 0
    for record in commits:
        parts = record.split("\t")
        if len(parts) < 3:
            continue
        emails = [e for e in parts[1:3] if PERSONAL_EMAIL_DOMAIN.search(e.rsplit("@", 1)[-1])]
        if emails:
            personal_emails.update(emails)
            commits_exposing_personal_email += 1

    refs = []
    paths = {}
    for row in objects:
        sha, _, path = row.partition(" ")
        if path:
            paths.setdefault(sha, path)
        refs.append(sha)

    found = []
    binary_count = large_count = scanned_count = 0
    proc = subprocess.Popen(["git", "-C", str(root), "cat-file", "--batch"],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for sha in refs:
            assert proc.stdin is not None and proc.stdout is not None
            proc.stdin.write((sha + "\n").encode("ascii"))
            proc.stdin.flush()
            header = proc.stdout.readline().split()
            if len(header) < 3 or header[1] == b"missing":
                raise RuntimeError("Unexpected Git object response")
            size = int(header[2]); obj_type = header[1]
            data = proc.stdout.read(size)
            if len(data) != size or proc.stdout.read(1) != b"\n":
                raise RuntimeError("Truncated Git object stream")
            if obj_type != b"blob":
                continue
            path = paths.get(sha, "<unknown historical path>")
            if size > 2_000_000:
                large_count += 1
                continue
            if b"\x00" in data[:4096]:
                binary_count += 1
                continue
            scanned_count += 1
            for name, pattern in COMPILED.items():
                match = pattern.search(data)
                if match:
                    line = data.count(b"\n", 0, match.start()) + 1
                    found.append((path, line, name))
    finally:
        if proc.stdin:
            proc.stdin.close()
        if proc.stdout:
            proc.stdout.close()
        proc.wait()

    suspicious_paths = sorted({p for p in paths.values() if RISKY_FILENAME.search(p)})
    print("Pre-publication audit of Git history reachable from locally fetched refs")
    print(f"Commits: {len(commits)}; text blobs scanned: {scanned_count}; binary skipped: {binary_count}; too large: {large_count}")
    print(f"Commits with personal-mailbox author/committer addresses: {commits_exposing_personal_email} (unique: {len(personal_emails)}; addresses hidden)")
    print(f"High-risk filename matches: {len(suspicious_paths)}")
    for p in suspicious_paths[:30]:
        print(f"  REVIEW filename: {p}")
    print(f"Possible hardcoded secrets: {len(found)}")
    for p, line, name in found[:30]:
        print(f"  REVIEW blob: {p}:{line} type={name} (value redacted)")
    print("This check cannot certify copyright compliance, image provenance, GitHub secrets,")
    print("workflow logs, unpublished PR refs, or deleted/unfetched Git history.")
    if found or suspicious_paths or personal_emails:
        print("REVIEW REQUIRED: do not publish based only on this automated scan.")
        return 1
    print("No listed patterns found in inspected blobs/commits; manual review still required.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
