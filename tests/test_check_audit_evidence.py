import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


CHECKER = Path(__file__).with_name("check_audit_evidence.py")
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
SHA = "0123456789abcdef0123456789abcdef01234567"


class EvidenceFixture:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.feature = root / "specs" / "005-fix-audit-findings"
        self.feature.mkdir(parents=True)
        self.artifact = self.feature / "artifact.log"
        self.artifact.write_text("verified\n", encoding="utf-8")
        self.rows = [self.make_row(f"CUR-{number:03d}") for number in range(1, 20)]
        self.rows += [self.make_row(f"AUD-{number:03d}") for number in range(1, 74)]
        self.evidence = self.feature / "evidence.md"
        self.write()

    @staticmethod
    def make_row(finding: str) -> dict[str, str]:
        return {
            "Finding": finding,
            "Requirements": "FR-001",
            "Root revision": f"{SHA} (clean)",
            "Transport revision": "NOT_APPLICABLE: root-only",
            "Method": "host-production",
            "Command": "`make -C tests test`",
            "Evidence location": f"`artifact.log#{finding}`",
            "Result": "PASS",
            "Observed at": "2026-07-23T12:00:00Z",
            "Notes": "Production-linked regression passed.",
            "Release blocker": "NO",
        }

    def row(self, finding: str) -> dict[str, str]:
        return next(row for row in self.rows if row["Finding"] == finding)

    def write(self) -> None:
        lines = [
            "# Feature 005 Release Evidence",
            "",
            "| " + " | ".join(HEADERS) + " |",
            "|" + "|".join("-" * (len(header) + 2) for header in HEADERS) + "|",
        ]
        lines.extend(
            "| " + " | ".join(row[header] for header in HEADERS) + " |"
            for row in self.rows
        )
        self.evidence.write_text("\n".join(lines) + "\n", encoding="utf-8")


class CheckAuditEvidenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.fixture = EvidenceFixture(Path(self.temporary_directory.name))

    def run_checker(self, *arguments: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), str(self.fixture.evidence), *map(str, arguments)],
            text=True,
            capture_output=True,
            check=False,
        )

    def assert_rejected(self, result: subprocess.CompletedProcess[str], message: str) -> None:
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(message, result.stdout + result.stderr)

    def test_accepts_complete_canonical_matrix(self) -> None:
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_detects_missing_cur_and_aud_identifiers(self) -> None:
        for finding in ("CUR-019", "AUD-073"):
            with self.subTest(finding=finding):
                fixture = EvidenceFixture(self.fixture.root / finding)
                fixture.rows.remove(fixture.row(finding))
                fixture.write()
                result = subprocess.run(
                    [sys.executable, str(CHECKER), str(fixture.evidence)],
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assert_rejected(result, f"missing identifier: {finding}")

    def test_detects_duplicate_identifier(self) -> None:
        self.fixture.rows.append(self.fixture.row("CUR-001").copy())
        self.fixture.write()
        self.assert_rejected(self.run_checker(), "duplicate identifier: CUR-001")

    def test_rejects_empty_required_fields(self) -> None:
        for field in HEADERS:
            with self.subTest(field=field):
                fixture = EvidenceFixture(self.fixture.root / field.replace(" ", "_"))
                fixture.row("AUD-001")[field] = ""
                fixture.write()
                result = subprocess.run(
                    [sys.executable, str(CHECKER), str(fixture.evidence)],
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assert_rejected(result, f"empty required field: {field}")

    def test_rejects_invalid_fr_list_and_revision(self) -> None:
        self.fixture.row("AUD-001")["Requirements"] = "SC-001"
        self.fixture.row("AUD-002")["Root revision"] = "abc123 (clean)"
        self.fixture.write()
        result = self.run_checker()
        self.assert_rejected(result, "invalid Requirements")
        self.assert_rejected(result, "invalid Root revision")

    def test_rejects_missing_evidence_artifact_for_pass(self) -> None:
        self.fixture.row("AUD-001")["Evidence location"] = "`missing.log#AUD-001`"
        self.fixture.write()
        self.assert_rejected(self.run_checker(), "missing evidence artifact")

    def test_rejects_model_only_evidence_for_production_requirement(self) -> None:
        row = self.fixture.row("AUD-001")
        row["Requirements"] = "FR-023"
        row["Method"] = "model-non-production"
        self.fixture.write()
        self.assert_rejected(self.run_checker(), "model-only evidence cannot satisfy FR-023")

    def test_rejects_host_only_evidence_for_hardware_requirement(self) -> None:
        row = self.fixture.row("AUD-001")
        row["Requirements"] = "FR-024"
        row["Method"] = "host-production, asan-ubsan"
        self.fixture.write()
        self.assert_rejected(self.run_checker(), "host-only evidence cannot satisfy FR-024")

    def test_rejects_superseded_without_passing_replacement(self) -> None:
        row = self.fixture.row("AUD-001")
        row["Result"] = "SUPERSEDED"
        row["Notes"] = "Older finding grouping."
        self.fixture.write()
        self.assert_rejected(self.run_checker(), "SUPERSEDED requires a passing replacement")

    def test_rejects_not_applicable_without_verified_scope_reason(self) -> None:
        row = self.fixture.row("AUD-001")
        row["Result"] = "NOT_APPLICABLE"
        row["Notes"] = "Not needed."
        self.fixture.write()
        self.assert_rejected(self.run_checker(), "NOT_APPLICABLE requires a verified scope reason")

    def test_accepts_not_applicable_with_verified_chip_scope(self) -> None:
        row = self.fixture.row("AUD-001")
        row["Result"] = "NOT_APPLICABLE"
        row["Notes"] = "Verified chip scope: W6300-only behavior does not apply to W5500."
        self.fixture.write()
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_release_blocker_yes_for_valid_reconciliation(self) -> None:
        row = self.fixture.row("AUD-001")
        row["Result"] = "NOT_APPLICABLE"
        row["Notes"] = "Verified path scope reason: root-only path has no transport behavior."
        row["Release blocker"] = "YES"
        self.fixture.write()
        self.assert_rejected(self.run_checker(), "valid reconciliation cannot be a release blocker")

    def test_rejects_release_blocker_yes_with_pass(self) -> None:
        self.fixture.row("AUD-001")["Release blocker"] = "YES"
        self.fixture.write()
        self.assert_rejected(self.run_checker(), "PASS cannot be a release blocker")

    def test_rejects_documentation_claim_stronger_than_evidence(self) -> None:
        row = self.fixture.row("CUR-001")
        row["Result"] = "BLOCKED"
        row["Release blocker"] = "YES"
        self.fixture.write()
        (self.fixture.root / "AUDIT-RESOLVED.md").write_text(
            "# Audit\n\nAll 73 historical findings resolved.\n", encoding="utf-8"
        )
        self.assert_rejected(self.run_checker(), "documentation claims release completion")

    def test_does_not_treat_scoped_complete_status_as_release_completion(self) -> None:
        row = self.fixture.row("CUR-001")
        row["Result"] = "BLOCKED"
        row["Release blocker"] = "YES"
        self.fixture.write()
        (self.fixture.root / "TODO.md").write_text(
            "## Unrelated verification\n\n**Status: COMPLETE.**\n", encoding="utf-8"
        )
        result = self.run_checker()
        self.assertNotIn("documentation claims release completion", result.stdout + result.stderr)

    def test_traceability_covers_every_fr_sc_cur_and_aud_identifier(self) -> None:
        spec = self.fixture.feature / "spec.md"
        tasks = self.fixture.feature / "tasks.md"
        spec.write_text("FR-001 through FR-002; SC-001; CUR-001; AUD-001\n", encoding="utf-8")
        tasks.write_text("FR-002, SC-001, CUR-001, AUD-001\n", encoding="utf-8")

        result = self.run_checker(Path("--traceability"), spec, tasks)
        self.assert_rejected(result, "traceability missing: FR-002, SC-001")

        row = self.fixture.row("CUR-001")
        row["Requirements"] = "FR-001, FR-002"
        row["Notes"] = "SC-001 production-linked regression passed."
        self.fixture.write()
        result = self.run_checker(Path("--traceability"), spec, tasks)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
