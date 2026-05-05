# Branching strategy

mono uses **prefix-driven branches** to communicate intent and (in the
future) to drive automated version bumping at merge time. The model is
adapted from the sibling prison-escape-game project.

## TL;DR

Every branch name starts with one of these prefixes:

| Prefix | When to use | Version impact (future) |
|---|---|---|
| `patch/` | Bug fixes, small tweaks, perf wins that don't change behavior | Bumps PATCH (`0.1.0` → `0.1.1`) |
| `minor/` | New features, new screens, new mechanics, additive APIs | Bumps MINOR (`0.1.0` → `0.2.0`) |
| `major/` | Breaking changes — save format incompat, public API changes | Bumps MAJOR (`0.1.0` → `1.0.0`) |
| `chore/` | Refactors, tooling, internal cleanup with no user-visible impact | No version bump; PR can use `skip` for changelog |
| `docs/` | Docs-only changes (`docs/`, `README.md`, `.claude/rules-*.md`, lore) | No version bump |

Examples:
- `patch/fix-vestigia-stack-overflow`
- `minor/popup-confirm-flow`
- `major/save-format-v2`
- `chore/ci-pipeline-bootstrap`
- `docs/hoard-prevention-lore`

Branch names after the slash are kebab-case, ~30-50 chars, and describe
the change concretely. The pre-merge PR description carries the full
context; the branch name just needs to be searchable.

## Why prefixes

Three reasons:

1. **Intent is explicit at branch-creation time.** You decide "is this a
   bug fix, a feature, or a breaking change" before writing the code,
   not after. That decision shapes the implementation — patch-level work
   stays small; major-level work plans the migration.

2. **Future automation can act on it.** The release pipeline (when it
   lands) reads the branch prefix on a merged PR and bumps the version
   accordingly. No manual SemVer bookkeeping. No "I forgot to bump
   CMakeLists.txt" PRs.

3. **Reviewers triage by prefix.** A `patch/` PR gets a quick look. A
   `major/` PR gets a careful review of the migration story. Prefix
   sets expectations for review depth.

## SemVer rules

mono follows [Semantic Versioning 2.0](https://semver.org/spec/v2.0.0.html).
Pre-1.0 (the current state), the rules are softer — the API can change —
but the prefixes still drive the bump:

- **PATCH** for bug fixes that don't change observable behavior beyond
  fixing the bug. Examples: vestigia chunked-write fix (the corruption
  goes away, nothing else changes), Makefile parity-gate fix.
- **MINOR** for new features and additive changes. Examples: VESTIGIA
  popup confirm flow, autosave on wood arrival, new gameplay screens.
- **MAJOR** for breaking changes that require migration. Examples: save
  format version bump (existing saves can't be loaded by new code without
  a migrator), engine API changes that break out-of-tree platform ports.

When in doubt between PATCH and MINOR: a change that adds a new
behavior the player can choose to use is MINOR; a change that fixes
existing behavior is PATCH.

When in doubt between MINOR and MAJOR: if a player who upgrades will
notice anything they have to relearn or reconfigure, it's MAJOR. If the
upgrade is invisible, it's MINOR.

## chore/ vs docs/

Both produce no version bump. The distinction:

- `chore/` is for **internal-only code changes** — refactors that don't
  change behavior, build system tweaks, CI changes, tooling. Code is
  touched but the player doesn't notice anything.
- `docs/` is for **documentation-only changes** — `docs/*.md`,
  `README.md`, `.claude/rules-*.md`, comments, lore-only commits to
  the design tree (`docs/design/*.md`) that don't change game behavior.

Both can use `skip` in the PR's `## Changelog` section, since neither
ships player-visible change. A `docs/` branch that ALSO changes player-
facing copy in dialog tables is mis-classified — that's `minor/`.

## When the prefix doesn't fit

If your change spans categories (e.g. a new feature that also fixes a
bug discovered during implementation), pick the **highest** prefix
that applies. A `minor/` PR may include incidental bug fixes; a
`patch/` PR should not include new features. When in real doubt, ask
in the PR description and a reviewer will help retitle.

## Master is always green

`master` always has a working build on both Arduboy and SDL. CI's
job is to enforce this — the parity gate, format check, and smoke
test run on every PR before merge. Branch protection (when enabled)
will block force-pushes to master and require all CI status checks
to pass before merge.

The local pre-commit hook (`scripts/pre-commit.sh`) runs
`make verify-parity` on commits that touch shared code. That's the
first line of defense; CI is the second.

## Working with branches

Standard flow for a new change:

```
git checkout master
git pull
git checkout -b patch/fix-<concise-description>
# ... make changes, commit, push ...
git push -u origin patch/fix-<concise-description>
gh pr create --fill   # uses the PR template
```

The PR description gets pre-filled from `.github/pull_request_template.md`.
Fill in the Summary, Changelog, Platform parity sections; CI runs on push.
Once green, merge via the GitHub UI (squash-merge keeps history linear).

## What's deferred

A few pieces of the prison-break model are not yet adopted in mono:

- **Branch protection.** Not enabled yet. When enabled, the required
  status checks will be: format, arduboy build, SDL build + smoke,
  changelog-check.
- **Auto version bump.** Will land alongside `release.yml` when v0.1
  is ready. Until then, `CMakeLists.txt` has no version field and
  `CHANGELOG.md` only has [Unreleased].
- **Squash-merge enforcement.** The release pipeline parses the squash-
  merge commit message to find the source PR. Until that pipeline
  exists, merge style is your call (squash recommended for clean
  history).
- **Public releases mirror.** mono is private. When public releases
  ship, a separate mirror repo holds the .hex / .exe artifacts.

These are tracked as future work and don't block the current branching
model.
