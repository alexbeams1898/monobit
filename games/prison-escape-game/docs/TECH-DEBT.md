# Prison Break Game — Tech Debt

Known structural issues to fix at the root, not paper over. Add to this list any time
something is identified as architecturally wrong.

---

- **WeaponSpriteSystem yOffset assumes colliders do NOT scale with character.** The
  `wielderYOffset` math multiplies `sprite.src_h * wielderScale` but subtracts the raw
  `collider.height` (no scale). This is correct today because colliders are fixed in world
  units regardless of visual scale -- a 1.2x character still has a 32x32 collider. If a
  future feature scales colliders alongside the character's visual scale (giants,
  shrink debuff, etc.), this line will produce a wrong yOffset and the weapon will drift
  from the hand at non-1.0 scales. Revisit `src/systems/WeaponSpriteSystem.cpp`
  `wielderYOffset` calculation if collider scaling is ever introduced.

- **Pre-existing cppcheck failures on master** (introduced before CI's cppcheck step
  was added; the step is currently `continue-on-error: true`). Examples:
  - `main.cpp:77` — cppcheck flags `throw;` in the `terminateHandler` as
    `rethrowNoCurrentException`. The pattern is correct in context (it runs from
    `std::set_terminate` where `std::current_exception()` *is* set), but cppcheck's
    heuristic doesn't know that. Suppress with a `// cppcheck-suppress
    rethrowNoCurrentException` comment, or refactor to use `std::current_exception()`
    explicitly.
  - `ladder_test.cpp:122` — `style: knownConditionTrueFalse` on
    `REQUIRE(ws.current_wave == 1)` after the test sets `ws.current_wave = 1`. Tests
    are meant to assert a known value. Either `// cppcheck-suppress
    knownConditionTrueFalse` or restructure the test setup.
  - `projectile_test.cpp:221` — `style: constVariableReference` on `auto& inv`. The
    inventory is read-only after construction; declare `const auto& inv`.
  - Engine pre-existing style hits in `Engine.cpp`, `FontManager.cpp`,
    `CollisionSystem.cpp`, `TileMapLoader.cpp` (now in this game), `FlowFieldSystem.cpp`.
    Detail: see `cppcheck --project=build/compile_commands.json --enable=all` output.

  Fix at the root and flip the CI job to required (remove `continue-on-error: true`
  in `.github/workflows/ci.yml`).

- **Pre-existing lizard complexity warnings on master.** The CI lizard step runs with
  CCN 15 / 250 lines / 8 parameters thresholds. Many existing functions exceed these:
  `CombatSystem::update` (CCN 193, 596 NLOC) is the worst offender. Each warning is its
  own refactor target. The CI job is `continue-on-error: true` until the pile is
  cleaned. Flip to required when the count is zero.

  Decomposition order should be driven by *which functions get touched most* (by
  diff frequency in `git log --follow`), not by which are biggest — refactoring a
  600-line function nobody touches gains nothing.
