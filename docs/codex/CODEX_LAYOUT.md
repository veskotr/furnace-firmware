# Codex project-local layout

Verified 2026-07-13 against installed `codex-cli 0.144.3` and official OpenAI Codex documentation.

| Purpose | Supported repository-local location | Choice here |
| --- | --- | --- |
| Instructions | `AGENTS.md` or `AGENTS.override.md` from repository root toward the working directory | Root `AGENTS.md`; narrower `components/AGENTS.md` |
| Skills | `.agents/skills/<skill>/SKILL.md` | Fifteen focused project skills with optional `agents/openai.yaml` |
| Custom agents | `.codex/agents/<agent>.toml` | Eleven role agents; diagnostic/review roles are read-only |
| Project config | `.codex/config.toml` in a trusted project | Concurrency/depth only; no model pinning or global privilege escalation |
| Admin restrictions | Managed installation policy / `requirements.toml` | Not added: repository files cannot impose administrator policy |

## Discovery behavior

- Codex reads global instructions first, then project instructions from repository root toward the current directory. At each directory, `AGENTS.override.md` wins over `AGENTS.md`; the closest instructions take precedence. The default combined instruction limit is 32 KiB.
- Repository skills are discovered by scanning `.agents/skills` from the working directory toward the repository root. A skill requires `SKILL.md`; scripts, references, assets, and `agents/openai.yaml` are optional.
- Custom agents are standalone TOML files under `.codex/agents`. Required fields are `name`, `description`, and `developer_instructions`. Per-agent `sandbox_mode` narrows review agents to `read-only` and permits implementation agents to inherit repository workspace writes.
- `.codex/config.toml` is loaded only when the project is trusted. Relative config paths are resolved from `.codex/`. Authentication/provider and other protected settings remain user-controlled.

## Why earlier paths were replaced

The prior `agents/*.md` briefs and `skills/*` workflows were useful content but were not in discovery paths supported by this installation. Their non-overlapping guidance was merged into the TOML agents, focused skills, this guide, and the engineering standards. The unsupported duplicate trees were removed so future sessions have one source of truth.

## Permission posture

- Root sessions retain the user-selected sandbox and approval policy.
- Repository analyst, architect, investigators, and specialist reviewers declare `sandbox_mode = "read-only"`.
- Feature, fix, test, and later-refactor agents use `workspace-write`, still constrained by `AGENTS.md` and user authorization.
- No repository hook, MCP server, network grant, flash command, or administrator requirement is enabled automatically.

## Maintenance

Run `python3 tools/codex/verify_repository_setup.py` after changing agents, skills, links, maps, or templates. Also run `codex doctor --json --summary` and a strict-config smoke test when upgrading Codex. Recheck official conventions before changing canonical paths.
