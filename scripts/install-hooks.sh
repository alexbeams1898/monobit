#!/usr/bin/env bash
# Run once from the repo root to install local git hooks.
# Example: bash scripts/install-hooks.sh

cp scripts/hooks/pre-commit .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
echo "pre-commit hook installed — clang-format will run automatically on staged C++ files."
