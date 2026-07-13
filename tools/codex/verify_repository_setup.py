#!/usr/bin/env python3
"""Validate repository-local Codex context without third-party dependencies."""

from __future__ import annotations

import re
import subprocess
import sys
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

REQUIRED_AGENTS = {
    "repository-analyst",
    "firmware-architect",
    "feature-implementer",
    "bug-investigator",
    "bug-fixer",
    "safety-reviewer",
    "concurrency-reviewer",
    "control-system-reviewer",
    "modbus-device-reviewer",
    "test-engineer",
    "refactoring-agent",
}

REQUIRED_SKILLS = {
    "orient-furnace-repository",
    "analyze-firmware-impact",
    "implement-furnace-feature",
    "decide-furnace-architecture",
    "investigate-furnace-bug",
    "fix-furnace-defect",
    "review-furnace-safety",
    "review-freertos-concurrency",
    "review-modbus-devices",
    "review-pid-profiles",
    "create-firmware-regression-test",
    "plan-hardware-validation",
    "refactor-furnace-behavior-preserving",
    "maintain-furnace-docs-map",
    "review-furnace-release",
}

REQUIRED_FILES = {
    "AGENTS.md",
    "components/AGENTS.md",
    ".codex/config.toml",
    "docs/codex/CODEX_LAYOUT.md",
    "docs/codex/REPOSITORY_MAP.md",
    "docs/codex/repository-map.yaml",
    "docs/codex/ENGINEERING_STANDARDS.md",
    "docs/analysis/FIRMWARE_FINDINGS.md",
    "docs/decisions/README.md",
    "docs/templates/bug-report.md",
    "docs/templates/feature-proposal.md",
    "docs/templates/architectural-decision-record.md",
    "docs/templates/change-validation-report.md",
}

FORBIDDEN_SCAFFOLD = re.compile(r"\b(TODO|TBD|FIXME|PLACEHOLDER)\s*[:\]]", re.IGNORECASE)
MARKDOWN_LINK = re.compile(r"\[[^]]+\]\(([^)]+)\)")


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def parse_skill_frontmatter(path: Path, errors: list[str]) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n"):
        fail(errors, f"missing YAML frontmatter: {path.relative_to(ROOT)}")
        return {}
    try:
        raw = text.split("---\n", 2)[1]
    except IndexError:
        fail(errors, f"unterminated YAML frontmatter: {path.relative_to(ROOT)}")
        return {}
    result: dict[str, str] = {}
    for line in raw.splitlines():
        if ":" in line:
            key, value = line.split(":", 1)
            result[key.strip()] = value.strip()
    return result


def validate_links(errors: list[str]) -> None:
    for path in ROOT.rglob("*.md"):
        if any(part in {".git", "build", "managed_components"} for part in path.parts):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for target in MARKDOWN_LINK.findall(text):
            target = target.strip().split("#", 1)[0]
            if not target or "://" in target or target.startswith("mailto:"):
                continue
            resolved = (path.parent / target).resolve()
            if not resolved.exists():
                fail(errors, f"broken link in {path.relative_to(ROOT)}: {target}")


def main() -> int:
    errors: list[str] = []

    for relative in sorted(REQUIRED_FILES):
        path = ROOT / relative
        if not path.is_file() or path.stat().st_size == 0:
            fail(errors, f"required non-empty file missing: {relative}")

    config_path = ROOT / ".codex/config.toml"
    if config_path.is_file():
        try:
            config = tomllib.loads(config_path.read_text(encoding="utf-8"))
            agents_cfg = config.get("agents", {})
            if agents_cfg.get("max_threads") != 4 or agents_cfg.get("max_depth") != 1:
                fail(errors, ".codex/config.toml must keep max_threads=4 and max_depth=1")
        except (tomllib.TOMLDecodeError, OSError) as exc:
            fail(errors, f"invalid .codex/config.toml: {exc}")

    agent_dir = ROOT / ".codex/agents"
    found_agents = {p.stem for p in agent_dir.glob("*.toml")}
    if found_agents != REQUIRED_AGENTS:
        fail(errors, f"agent set mismatch: missing={sorted(REQUIRED_AGENTS-found_agents)}, extra={sorted(found_agents-REQUIRED_AGENTS)}")
    for path in agent_dir.glob("*.toml"):
        try:
            data = tomllib.loads(path.read_text(encoding="utf-8"))
        except tomllib.TOMLDecodeError as exc:
            fail(errors, f"invalid agent TOML {path.relative_to(ROOT)}: {exc}")
            continue
        for field in ("name", "description", "developer_instructions"):
            if not isinstance(data.get(field), str) or not data[field].strip():
                fail(errors, f"agent {path.stem} missing {field}")
        if data.get("sandbox_mode") not in {"read-only", "workspace-write"}:
            fail(errors, f"agent {path.stem} has invalid sandbox_mode")

    skill_dir = ROOT / ".agents/skills"
    found_skills = {p.name for p in skill_dir.iterdir() if p.is_dir()} if skill_dir.is_dir() else set()
    if found_skills != REQUIRED_SKILLS:
        fail(errors, f"skill set mismatch: missing={sorted(REQUIRED_SKILLS-found_skills)}, extra={sorted(found_skills-REQUIRED_SKILLS)}")
    for name in sorted(found_skills):
        skill_path = skill_dir / name / "SKILL.md"
        ui_path = skill_dir / name / "agents/openai.yaml"
        if not skill_path.is_file() or not ui_path.is_file():
            fail(errors, f"skill {name} missing SKILL.md or agents/openai.yaml")
            continue
        metadata = parse_skill_frontmatter(skill_path, errors)
        if metadata.get("name") != name:
            fail(errors, f"skill directory/frontmatter mismatch: {name}")
        description = metadata.get("description", "")
        if len(description) < 40 or "Use " not in description or "Do not use" not in description:
            fail(errors, f"skill {name} description lacks use/non-use routing")
        body = skill_path.read_text(encoding="utf-8")
        for required in ("## Inputs", "## Workflow", "## Required output", "## Stop and safeguards"):
            if required not in body:
                fail(errors, f"skill {name} missing section: {required}")
        ui = ui_path.read_text(encoding="utf-8")
        if f"${name}" not in ui or "display_name:" not in ui or "short_description:" not in ui:
            fail(errors, f"skill {name} has incomplete UI metadata")

    scan_paths = [ROOT / "AGENTS.md", ROOT / "components/AGENTS.md", ROOT / "docs/codex", ROOT / ".codex", ROOT / ".agents/skills"]
    for base in scan_paths:
        paths = [base] if base.is_file() else [p for p in base.rglob("*") if p.is_file()]
        for path in paths:
            text = path.read_text(encoding="utf-8", errors="replace")
            if FORBIDDEN_SCAFFOLD.search(text):
                fail(errors, f"scaffold marker remains: {path.relative_to(ROOT)}")

    map_text = (ROOT / "docs/codex/REPOSITORY_MAP.md").read_text(encoding="utf-8") if (ROOT / "docs/codex/REPOSITORY_MAP.md").is_file() else ""
    for command in ("idf.py build", "idf.py -p PORT flash monitor"):
        if command not in map_text:
            fail(errors, f"build/flash command not documented: {command}")

    standards = (ROOT / "docs/codex/ENGINEERING_STANDARDS.md").read_text(encoding="utf-8") if (ROOT / "docs/codex/ENGINEERING_STANDARDS.md").is_file() else ""
    for invariant in ("de-energized", "fresh, valid temperature", "physical-output boundary"):
        if invariant not in standards:
            fail(errors, f"safety invariant missing phrase: {invariant}")

    layout = (ROOT / "docs/codex/CODEX_LAYOUT.md").read_text(encoding="utf-8") if (ROOT / "docs/codex/CODEX_LAYOUT.md").is_file() else ""
    try:
        version = subprocess.run(["codex", "--version"], cwd=ROOT, check=True, capture_output=True, text=True, timeout=10).stdout.strip()
        if "0.144.3" not in version or "0.144.3" not in layout:
            fail(errors, f"Codex layout/version mismatch: installed={version!r}")
    except (FileNotFoundError, subprocess.SubprocessError) as exc:
        fail(errors, f"cannot verify installed Codex version: {exc}")

    for relative in (
        "main/main.c",
        "components/coordinator_component/src/coordinator_component_heater_controller.c",
        "components/heater_controller_component/src/heater_controller_task.c",
        "components/temperature_processor_component/src/temperature_processor_task.c",
        "components/nextion_hmi/src/coordinator/hmi_coordinator.c",
    ):
        if not (ROOT / relative).is_file():
            fail(errors, f"mapped source path missing: {relative}")

    validate_links(errors)

    if errors:
        print(f"repository setup verification: FAIL ({len(errors)} errors)")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"repository setup verification: PASS ({len(REQUIRED_AGENTS)} agents, {len(REQUIRED_SKILLS)} skills)")
    print("Codex layout: 0.144.3-compatible project-local discovery paths")
    print("Maps, templates, safety invariants, metadata, links, paths, and commands: valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
