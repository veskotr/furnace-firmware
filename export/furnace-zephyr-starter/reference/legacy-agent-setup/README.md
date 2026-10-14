# Legacy agent setup: read-only reference

These are verbatim copies of the legacy repository's agent instructions, agent definitions, skills, templates and verification script, as they stood at hand-over (2026-10-13).

**Rules for this folder:**

- **Do not modify these files.** They are a record.
- **Do not install them** in the new repository and **do not copy-paste them** as the new rules, skills or agents. They describe an ESP-IDF/FreeRTOS hardening phase, legacy paths and legacy documents that do not exist in the new project.
- **Use them as source material.** Read them for intent, structure and safeguards, then author new ones for Zephyr and test-first work, following [../../08-SETUP-FOR-NEW-REPO.md](../../08-SETUP-FOR-NEW-REPO.md) §4.

The files were renamed so tools will not auto-discover them as live configuration:

| Here | Original location in the legacy repository | Original role |
| --- | --- | --- |
| `instructions/legacy-root-instructions.md` | `AGENTS.md` | Root agent instructions |
| `instructions/legacy-components-instructions.md` | `components/AGENTS.md` | Per-directory instructions |
| `instructions/legacy-engineering-standards.md` | `docs/codex/ENGINEERING_STANDARDS.md` | Safety and concurrency standards |
| `instructions/legacy-codex-layout.md` | `docs/codex/CODEX_LAYOUT.md` | How Codex discovered instructions, skills and agents |
| `instructions/legacy-codex-config.toml` | `.codex/config.toml` | Codex project config (threads, depth) |
| `agents/<name>.agent.toml` | `.codex/agents/<name>.toml` | 11 Codex agent definitions |
| `skills/<name>.skill.md` | `.agents/skills/<name>/SKILL.md` | 15 skills |
| `skills/<name>.openai.yaml` | `.agents/skills/<name>/agents/openai.yaml` | Skill UI descriptors |
| `templates/*.md` | `docs/templates/*.md` | Bug, feature, ADR and change-validation templates |
| `tools/verify_repository_setup.py.txt` | `tools/codex/verify_repository_setup.py` | Repository setup verification (renamed to `.txt`; not meant to run here) |

Paths mentioned inside these files (for example `docs/handoff/...`, `docs/analysis/...`, `components/...`) refer to the **legacy** repository. Their content is available as text in this starter's numbered documents.
