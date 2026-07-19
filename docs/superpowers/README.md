# Superpowers docs (tracked design archive)

`docs/` is gitignored for local agent scratch; a small set of design artifacts is force-tracked.

## Keep

| Path | Role |
|------|------|
| `prd/` | Product requirements |
| `specs/` | Design specs (source of truth for past decisions) |

## Removed from tree (completed work)

Execution **plans/** and verification **reports/** were deleted after landing to keep `main` clean. History remains in git if you need them (`git log -- docs/superpowers/plans`).

## Local-only (not committed)

Agent ledgers under `.superpowers/`, worktrees, and IDE configs (`.agents/`, `.claude/`, etc.).
