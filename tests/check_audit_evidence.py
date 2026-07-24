#!/usr/bin/env python3
"""Validate the canonical Feature 005 release-evidence matrix."""

import argparse
import html
import re
import sys
from collections import Counter
from pathlib import Path


HEADING = "# Feature 005 Release Evidence"
HEADERS = (
    "Finding",
    "Requirements",
    "Root revision",
    "Transport revision",
    "Method",
    "Command",
    "Evidence location",
    "Result",
    "Observed at",
    "Notes",
    "Release blocker",
)
EXPECTED_IDS = {
    *(f"CUR-{number:03d}" for number in range(1, 20)),
    *(f"AUD-{number:03d}" for number in range(1, 74)),
}
ACCEPTED_METHODS = {
    "host-production",
    "asan-ubsan",
    "tsan",
    "cbmc-production",
    "static-analysis",
    "cross-compile",
    "optimized-check",
    "hardware-smoke",
    "hardware-diagnostic",
    "model-non-production",
}
HARDWARE_METHODS = {"hardware-smoke", "hardware-diagnostic"}
RESULTS = {"PASS", "FAIL", "BLOCKED", "NOT_APPLICABLE", "SUPERSEDED"}
REVISION_RE = re.compile(r"^[0-9a-fA-F]{40} \((clean|dirty)\)$")
TIMESTAMP_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
FINDING_RE = re.compile(r"^(?:CUR-\d{3}|AUD-\d{3})$")
FR_LIST_RE = re.compile(r"^FR-\d{3}(?:,\s*FR-\d{3})*$")
IDENTIFIER_RE = re.compile(r"\b(?:FR|SC|CUR|AUD)-\d{3}\b")
RANGE_RE = re.compile(
    r"\b(FR|SC|CUR|AUD)-(\d{3})\s*(?:through|[-\u2013\u2014])\s*"
    r"(?:\1-)?(\d{3})\b",
    re.IGNORECASE,
)


def parse_table(path: Path) -> tuple[list[dict[str, str]], list[str]]:
    errors: list[str] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        return [], [f"cannot read evidence file: {exc}"]

    if HEADING not in lines:
        errors.append(f"missing exact heading: {HEADING}")

    expected_header = "| " + " | ".join(HEADERS) + " |"
    try:
        header_index = lines.index(expected_header)
    except ValueError:
        return [], errors + ["missing exact canonical table header"]

    if header_index + 1 >= len(lines):
        return [], errors + ["missing canonical table separator"]
    separator = split_row(lines[header_index + 1])
    if len(separator) != len(HEADERS) or any(
        re.fullmatch(r":?-{3,}:?", cell) is None for cell in separator
    ):
        errors.append("invalid canonical table separator")

    rows: list[dict[str, str]] = []
    for line_number, line in enumerate(lines[header_index + 2 :], header_index + 3):
        if not line.startswith("|"):
            if line.strip():
                break
            continue
        cells = split_row(line)
        if len(cells) != len(HEADERS):
            errors.append(
                f"line {line_number}: expected {len(HEADERS)} fields, found {len(cells)}"
            )
            continue
        rows.append(dict(zip(HEADERS, cells)))
    return rows, errors


def split_row(line: str) -> list[str]:
    if not line.startswith("|") or not line.endswith("|"):
        return []
    return [html.unescape(cell.strip()) for cell in line[1:-1].split("|")]


def split_list(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def split_breaks(value: str) -> list[str]:
    return [item.strip() for item in re.split(r"<br\s*/?>", value) if item.strip()]


def validate_rows(rows: list[dict[str, str]], evidence_path: Path) -> list[str]:
    errors: list[str] = []
    for index, row in enumerate(rows, 1):
        finding = row["Finding"] or f"row {index}"
        for field in HEADERS:
            if not row[field].strip():
                errors.append(f"{finding}: empty required field: {field}")

    identifiers = [row["Finding"] for row in rows if row["Finding"]]
    counts = Counter(identifiers)
    for identifier in sorted(EXPECTED_IDS - set(identifiers)):
        errors.append(f"missing identifier: {identifier}")
    for identifier, count in sorted(counts.items()):
        if count > 1:
            errors.append(f"duplicate identifier: {identifier}")
    for identifier in sorted(set(identifiers) - EXPECTED_IDS):
        errors.append(f"unexpected identifier: {identifier}")

    rows_by_id = {
        row["Finding"]: row
        for row in rows
        if FINDING_RE.fullmatch(row["Finding"]) and counts[row["Finding"]] == 1
    }
    for row in rows:
        errors.extend(validate_row(row, rows_by_id, evidence_path))
    return errors


def validate_row(
    row: dict[str, str], rows_by_id: dict[str, dict[str, str]], evidence_path: Path
) -> list[str]:
    errors: list[str] = []
    finding = row["Finding"] or "unknown row"
    requirements = split_list(row["Requirements"])
    methods = split_list(row["Method"])
    result = row["Result"]
    blocker = row["Release blocker"]

    if row["Finding"] and FINDING_RE.fullmatch(row["Finding"]) is None:
        errors.append(f"{finding}: invalid Finding")
    if row["Requirements"] and FR_LIST_RE.fullmatch(row["Requirements"]) is None:
        errors.append(f"{finding}: invalid Requirements; expected comma-separated FR identifiers")
    if row["Root revision"] and REVISION_RE.fullmatch(row["Root revision"]) is None:
        errors.append(f"{finding}: invalid Root revision")
    transport = row["Transport revision"]
    if transport and transport != "NOT_APPLICABLE: root-only" and REVISION_RE.fullmatch(transport) is None:
        errors.append(f"{finding}: invalid Transport revision")
    unknown_methods = sorted(set(methods) - ACCEPTED_METHODS)
    if unknown_methods:
        errors.append(f"{finding}: unaccepted Method: {', '.join(unknown_methods)}")
    if result and result not in RESULTS:
        errors.append(f"{finding}: invalid Result: {result}")
    if row["Observed at"] and TIMESTAMP_RE.fullmatch(row["Observed at"]) is None:
        errors.append(f"{finding}: invalid Observed at timestamp")
    if blocker and blocker not in {"YES", "NO"}:
        errors.append(f"{finding}: invalid Release blocker: {blocker}")

    if result == "PASS":
        if blocker == "YES":
            errors.append(f"{finding}: PASS cannot be a release blocker")
        if not row["Root revision"].endswith(" (clean)"):
            errors.append(f"{finding}: PASS requires a clean Root revision")
        if transport != "NOT_APPLICABLE: root-only" and not transport.endswith(" (clean)"):
            errors.append(f"{finding}: PASS requires a clean Transport revision")
        errors.extend(validate_artifacts(finding, row["Evidence location"], evidence_path))
    elif blocker == "NO" and (finding.startswith("CUR-") or result in {"FAIL", "BLOCKED"}):
        errors.append(f"{finding}: {result or 'non-PASS result'} must be a release blocker")

    if finding.startswith("CUR-") and result != "PASS":
        errors.append(f"{finding}: current findings must be PASS")
    if finding.startswith("AUD-") and result in {"FAIL", "BLOCKED"}:
        errors.append(f"{finding}: historical FAIL/BLOCKED blocks reconciliation")

    reconciliation_valid: bool | None = None
    if result == "SUPERSEDED":
        supersession_errors = validate_supersession(row, rows_by_id)
        errors.extend(supersession_errors)
        reconciliation_valid = not supersession_errors
    if result == "NOT_APPLICABLE":
        reconciliation_valid = re.search(
            r"\bverified\s+(?:(?:chip|path)\s+)?scope(?:\s+reason)?\s*:",
            row["Notes"],
            re.IGNORECASE,
        ) is not None
        if not reconciliation_valid:
            errors.append(f"{finding}: NOT_APPLICABLE requires a verified scope reason")
    if finding.startswith("AUD-") and reconciliation_valid is True and blocker == "YES":
        errors.append(f"{finding}: valid reconciliation cannot be a release blocker")
    if finding.startswith("AUD-") and reconciliation_valid is False and blocker == "NO":
        errors.append(f"{finding}: invalid reconciliation must be a release blocker")

    if result == "PASS" and "FR-023" in requirements and set(methods) <= {
        "model-non-production"
    }:
        errors.append(f"{finding}: model-only evidence cannot satisfy FR-023")
    if result == "PASS" and "FR-024" in requirements and not set(methods) & HARDWARE_METHODS:
        errors.append(f"{finding}: host-only evidence cannot satisfy FR-024")

    commands = split_breaks(row["Command"])
    locations = split_breaks(row["Evidence location"])
    if methods and (len(commands) != len(methods) or len(locations) != len(methods)):
        errors.append(
            f"{finding}: Method, Command, and Evidence location entries must have matching counts"
        )
    return errors


def validate_artifacts(finding: str, value: str, evidence_path: Path) -> list[str]:
    errors: list[str] = []
    for reference in split_breaks(value):
        reference = reference.strip("`")
        artifact_name = reference.split("#", 1)[0]
        artifact = Path(artifact_name)
        if not artifact.is_absolute():
            artifact = evidence_path.parent / artifact
        if not artifact.exists():
            errors.append(f"{finding}: missing evidence artifact: {artifact_name}")
    return errors


def validate_supersession(
    row: dict[str, str], rows_by_id: dict[str, dict[str, str]]
) -> list[str]:
    finding = row["Finding"]
    replacements = [
        identifier
        for identifier in IDENTIFIER_RE.findall(row["Notes"])
        if identifier != finding and identifier.startswith(("CUR-", "AUD-"))
    ]
    for replacement in replacements:
        replacement_row = rows_by_id.get(replacement)
        if replacement_row is None or replacement_row["Result"] != "PASS":
            continue
        original_requirements = set(split_list(row["Requirements"]))
        replacement_requirements = set(split_list(replacement_row["Requirements"]))
        if original_requirements <= replacement_requirements:
            return []
    return [f"{finding}: SUPERSEDED requires a passing replacement covering its requirements"]


def canonical_repository_root(evidence_path: Path) -> Path | None:
    feature = evidence_path.resolve().parent
    if feature.parent.name != "specs":
        return None
    return feature.parent.parent


def validate_status_documents(
    rows: list[dict[str, str]], evidence_path: Path
) -> list[str]:
    blocked = {
        row["Finding"]
        for row in rows
        if row["Result"] not in {"PASS", "SUPERSEDED", "NOT_APPLICABLE"}
    }
    if not blocked:
        return []
    root = canonical_repository_root(evidence_path)
    if root is None:
        return []

    documents = (
        root / "TODO.md",
        root / "AUDIT-RESOLVED.md",
        root / "docs" / "security" / "SECURITY-REVIEW-2026-07-21.md",
    )
    errors: list[str] = []
    completion_re = re.compile(
        r"\brelease(?: is)? ready\b|"
        r"\ball\b[^\n.]{0,80}\bfindings?\b[^\n.]{0,30}\b(?:resolved|fixed|complete)\b",
        re.IGNORECASE,
    )
    for document in documents:
        if not document.is_file():
            continue
        text = document.read_text(encoding="utf-8")
        if completion_re.search(text):
            errors.append(
                f"{document}: documentation claims release completion while evidence is blocked"
            )
        for line_number, line in enumerate(text.splitlines(), 1):
            mentioned = blocked & set(IDENTIFIER_RE.findall(line))
            if mentioned and re.search(r"\[x\]|\b(?:resolved|complete|fixed)\b", line, re.IGNORECASE):
                errors.append(
                    f"{document}:{line_number}: documentation claims stronger status for "
                    + ", ".join(sorted(mentioned))
                )
    return errors


def extract_identifiers(text: str) -> set[str]:
    identifiers = set(IDENTIFIER_RE.findall(text.upper()))
    for prefix, start_text, end_text in RANGE_RE.findall(text):
        start = int(start_text)
        end = int(end_text)
        if start <= end:
            identifiers.update(f"{prefix.upper()}-{number:03d}" for number in range(start, end + 1))
    return identifiers


def validate_traceability(
    rows: list[dict[str, str]], source_paths: tuple[Path, Path]
) -> list[str]:
    required: set[str] = set()
    errors: list[str] = []
    for path in source_paths:
        try:
            required.update(extract_identifiers(path.read_text(encoding="utf-8")))
        except OSError as exc:
            errors.append(f"cannot read traceability source {path}: {exc}")
    validated_text = "\n".join(
        " ".join(row.values())
        for row in rows
        if row["Result"] in {"PASS", "SUPERSEDED", "NOT_APPLICABLE"}
    )
    covered = extract_identifiers(validated_text)
    missing = sorted(required - covered)
    if missing:
        errors.append("traceability missing: " + ", ".join(missing))
    return errors


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument(
        "--traceability",
        nargs=2,
        metavar=("SPEC", "TASKS"),
        type=Path,
        help="cross-reference all FR/SC/CUR/AUD identifiers in SPEC and TASKS",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv if argv is not None else sys.argv[1:])
    rows, errors = parse_table(arguments.evidence)
    errors.extend(validate_rows(rows, arguments.evidence))
    errors.extend(validate_status_documents(rows, arguments.evidence))
    if arguments.traceability:
        errors.extend(validate_traceability(rows, tuple(arguments.traceability)))

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        print("RELEASE VERDICT: FAIL")
        return 1
    print(f"validated {len(rows)} evidence rows")
    print("RELEASE VERDICT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
