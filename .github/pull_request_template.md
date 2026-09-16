<!--
KEEP THIS SHORT. A reviewer scans; they do not read.

Summary: 1-3 sentences. What changed and why it was wrong before.
Never narrate the diff -- it is on the next tab.
Bullets over paragraphs. Bold the subject so the eye can skip.
If the body runs past ~20 lines, the PR is too big, not the description.

Detail belongs in the commit messages, where whoever runs `git log` in two
years will actually find it.
-->

## Summary

## Issue
Closes #

## Changelog
<!--
Format: - <Category>: <Description ending with a period.>
Categories: Added Changed Deprecated Removed Fixed Security Performance

Not required on `chore/`, `docs/`, or any `engine`-scope branch -- those bump
no version, and the branch name already says so. Omit the section entirely.
Everywhere else CI blocks the merge without it. `skip` for no user-facing change.
-->

- Added:

## Checklist
<!-- CI covers format, lint, tests, and the gates. These are the ones it cannot. -->
- [ ] Ran the thing, not just built it
- [ ] Tests added for logic a test can reach
