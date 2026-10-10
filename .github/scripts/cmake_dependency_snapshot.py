#!/usr/bin/env python3
"""Build a dependency-graph snapshot from CMake FetchContent pins.

Reads FetchContent_Declare blocks, keeps the ones that point at a GitHub
repository, and prints the JSON body expected by the dependency submission API:

    POST /repos/{owner}/{repo}/dependency-graph/snapshots

Only GitHub repositories are reported, because the snapshot needs a package-url
and pkg:github is the only type that maps to a GIT_TAG pin.

Usage:
    cmake_dependency_snapshot.py --repo owner/name --sha <sha> --ref <ref> \
        --correlator <key> [--output snapshot.json] [CMakeLists.txt ...]

Environment variables GITHUB_REPOSITORY, GITHUB_SHA, GITHUB_REF, GITHUB_WORKFLOW
and GITHUB_JOB are used as defaults, so inside Actions it runs with no arguments.
"""

import argparse
import json
import os
import re
import sys
from datetime import datetime, timezone
from urllib.parse import quote

DECLARE = re.compile(r"FetchContent_Declare\s*\((.*?)\)", re.DOTALL)
GIT_REPOSITORY = re.compile(r"GIT_REPOSITORY\s+(\S+)")
GIT_TAG = re.compile(r"GIT_TAG\s+(\S+)")
GITHUB_URL = re.compile(
    r"^https?://github\.com/(?P<owner>[^/\s]+)/(?P<repo>[^/\s]+?)(?:\.git)?/?$"
)
DEFAULT_FILES = ["CMakeLists.txt"]


def unquote(value):
    """Drop the surrounding quotes a CMake argument may carry."""
    return value.strip().strip('"').strip("'")


def find_pins(text):
    """Yield (repository_url, tag) for every FetchContent_Declare in text."""
    for block in DECLARE.findall(text):
        repository = GIT_REPOSITORY.search(block)
        tag = GIT_TAG.search(block)
        if repository and tag:
            yield unquote(repository.group(1)), unquote(tag.group(1))


def name_and_purl(url, tag):
    """Return (name, purl) for a GitHub repository URL, or None otherwise."""
    match = GITHUB_URL.match(url)
    if not match:
        return None
    owner = match.group("owner")
    repo = match.group("repo")
    version = tag[1:] if tag.startswith("v") else tag
    name = "{}/{}".format(owner, repo)
    purl = "pkg:github/{}/{}@{}".format(quote(owner), quote(repo), quote(version))
    return name, purl


def build_snapshot(sha, ref, correlator, detector_url, files):
    manifests = {}
    for path in files:
        if not os.path.isfile(path):
            print("skipping missing file: {}".format(path), file=sys.stderr)
            continue
        with open(path, encoding="utf-8") as handle:
            text = handle.read()

        resolved = {}
        for url, tag in find_pins(text):
            found = name_and_purl(url, tag)
            if not found:
                print("skipping non-GitHub dependency: {}".format(url), file=sys.stderr)
                continue
            name, purl = found
            resolved[name] = {
                "package_url": purl,
                "relationship": "direct",
                "scope": "runtime",
            }

        if resolved:
            manifests[path] = {
                "name": path,
                "file": {"source_location": path},
                "resolved": resolved,
            }

    return {
        "version": 0,
        "sha": sha,
        "ref": ref,
        "job": {"correlator": correlator, "id": sha},
        "detector": {
            "name": "cmake-fetchcontent",
            "version": "1.0.0",
            "url": detector_url,
        },
        "scanned": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "manifests": manifests,
    }


def main():
    parser = argparse.ArgumentParser(description="Emit a dependency snapshot for CMake FetchContent pins.")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--sha", default=os.environ.get("GITHUB_SHA", ""))
    parser.add_argument("--ref", default=os.environ.get("GITHUB_REF", ""))
    parser.add_argument(
        "--correlator",
        default="{} {}".format(
            os.environ.get("GITHUB_WORKFLOW", ""), os.environ.get("GITHUB_JOB", "")
        ).strip()
        or "cmake-fetchcontent",
    )
    parser.add_argument("--output", default=None, help="write here instead of stdout")
    parser.add_argument(
        "--allow-empty",
        action="store_true",
        help="do not fail when no GitHub FetchContent dependency is found",
    )
    parser.add_argument("files", nargs="*", default=DEFAULT_FILES)
    args = parser.parse_args()

    if not args.sha or not args.ref:
        parser.error("--sha and --ref are required (or set GITHUB_SHA and GITHUB_REF)")

    detector_url = (
        "https://github.com/{}".format(args.repo) if args.repo else "https://github.com"
    )
    snapshot = build_snapshot(args.sha, args.ref, args.correlator, detector_url, args.files)

    count = sum(len(manifest["resolved"]) for manifest in snapshot["manifests"].values())
    if count == 0 and not args.allow_empty:
        print("no GitHub FetchContent dependency found", file=sys.stderr)
        return 1

    payload = json.dumps(snapshot, indent=2, sort_keys=True) + "\n"
    if args.output:
        with open(args.output, "w", encoding="utf-8") as handle:
            handle.write(payload)
    else:
        sys.stdout.write(payload)
    print("{} dependencies".format(count), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
