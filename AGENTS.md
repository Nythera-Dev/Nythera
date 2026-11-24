# Nythera Shell Agent Instructions

Repository-specific guidance supplementing global agent instructions. Follow global rules alongside this document; do not treat this file as replacing them.

## Common development rules

### Workflow, scope and tool efficiency

- **Orient and inspect:** Check `git status --short` before edits. Identify the owning subsystem with `rg --files`, focused `rg -n` searches, and bounded `sed -n` reads. Exclude `third_party/`, build trees, generated output, caches, and unrelated large assets unless relevant.
- **Implement coherently:** Keep related edits together. Avoid touching unrelated code, speculative abstractions, unnecessary refactoring, or mass reformatting. For research-only requests, inspect and explain without editing.
- **Preserve behaviour and invariants:** Preserve dark sci-fi/fantasy aesthetics, configurable styling, Umbriel compatibility, and approved architectural decisions. Surface conflicts with documented technical invariants rather than silently bypassing them.
- **Tool efficiency:** Prefer repository source over external browsing; prefer direct Git, `rg`/`sed`, and focused patches over heavy scripts (use Python or shell only for structured processing or reproducible diagnostics). Keep compiler/test output in local logs and inspect excerpts rather than dumping full logs. Await completion without repeated polling; check exit codes. Do not use subagents for routine work; delegate only independent bounded work where coordination costs are justified. Reuse existing visual captures for unchanged visual states.

### Build, formatting and validation safeguards

- Use the existing `build-debug/` directory (`debugoptimized` default) and incremental `just build` (which configures if necessary; do not run `just configure` before every build).
- Do not run `just rebuild`, `just clean`, ASan, release builds, full `just lint`, or full `just test` by default.
- Format **only touched C++ files** under `src/` and `tests/` with the project's configured `clang-format` version (22+). Do not run repo-wide `just format-code` for narrow changes.
- Prefer focused `clang-tidy -p build-debug --warnings-as-errors='*' <touched-files>` when lint is warranted; whole-tree `just lint` is an explicit-request/CI operation.
- For docs, use the repository remark formatter or `just format-docs-check` when Markdown structure or syntax changes; note that the docs recipe may install its pinned local npm dependencies.
- Before a targeted Meson test, use `meson test -C build-debug --list` once if the exact test name is unknown, then run `meson test -C build-debug <test-name> --print-errorlogs`. Add a focused isolated regression test when protecting a deterministic contract not already covered; do not invent arbitrary tests merely to tick a box.
- **Review before finishing:** Run `git diff --check`, review the changed-file list and focused diff, check affected architectural invariants, and report actual checks run, scope, and unresolved items. Never cite unverified claims or inferred test passes.

- **Runtime compatibility during coordinated changes:** Before changing shared assets, resource layouts, public library interfaces or other files consumed at runtime, account for currently running applications and any subprocesses/helpers they may launch. Preserve compatibility with the active runtime until the coordinated build, installation and authorised restart are complete. If compatibility cannot be maintained, stop before the incompatible change and agree a safe transition and recovery plan with the maintainer. Validate affected runtime paths, including security-critical helpers where relevant, before removing temporary compatibility measures. Never weaken authentication or other safety checks to conceal a deployment mismatch.

### Authorisation and Git safety

- Do not modify live shell configuration (`~/.config/nythera`, `~/.local/state/nythera`), install host packages, modify global system configuration, restart running applications/services (`nythera restart`), commit, or push without explicit authorisation.
- Protect uncommitted work: do not reset, overwrite, stash, rebase, force-push, or merge into `main` without explicit approval and an accounted-for state.
- Use `dev` for ordinary development; create a temporary task branch or worktree only for substantial or risky changes.
- When authorised to commit or push "your changes", identify and stage the complete relevant session change set across implementation, tests, assets, and docs (never blindly staging the entire working tree; ask if ownership is uncertain). Verify the remote branch after pushing and report what was committed, what was pushed, and any relevant uncommitted files.

### Baseline test isolation and data safety

- All tests involving filesystem mutations or user/system services must operate exclusively on isolated, disposable fixtures—never touching live user config, home directory contents, or the live session bus.
- Any test constructing `ConfigService` or Shell service objects must isolate `HOME`, `NYTHERA_{CONFIG,STATE,DATA}_HOME`, `XDG_{CONFIG,STATE,DATA,CACHE}_HOME`, set `GIO_USE_VFS=local`, and disable or privately isolate `DBUS_SESSION_BUS_ADDRESS` before instantiation; never fall back silently to the live session bus.
- Create fixture roots exclusively (`mkdtemp` or exclusive `create_directory`) and clean up by exact owned path only—never via prefix or wildcard sweeps.
- Drain asynchronous workers and helper sessions cleanly before teardown; avoid `std::exit()` or unguarded paths that skip fixture disposal. Immediately stop and report any unintended user-data modification or isolation gap.

---

## Nythera Shell context and invariants

- **Nythera** is a C++23 native Wayland shell fork, built with **Meson/Ninja** and orchestrated by `just`—not the original Noctalia or a QML project.
- Development primarily targets **Umbriel**. Preserve other compositor integrations unless explicitly changing them; route compositor-specific behaviour through `CompositorPlatform` and its backend.
- Preserve upstream functionality and approved Nythera design decisions: floating islands, dynamic bars, screen wrapping, configurable styling, and dark sci-fi/fantasy aesthetics. Do not reinterpret approved behaviour or introduce unrelated redesigns.
- Distinguish Nythera-owned identifiers from Umbriel-owned integration contracts before renaming or modifying them.
- **Asset ownership:** `nythera-shell` owns Shell-specific artwork, branding, templates, and Shell-specific translations in `assets/`, while shared Nythera fonts, Tabler glyph metadata, and shared library translations are owned and resolved by **NytheraLib**.
- **Live UI verification:** Use `nythera restart` only after successful build/applicable tests, when necessary and explicitly authorised to interrupt the active shell. Never restart during a locked session or after a failed build/test. The user's visual review can be the final acceptance check; do not equate compilation with visual verification.

## Cross-repository dependencies (NytheraLib)

- `nythera-shell` dynamically links `nythera-lib` (`>=0.1.0, <0.2.0`) via `pkg-config` and remains independent of `nythera-files`.
- `justfile` automatically discovers `$HOME/.local/opt/nythera-lib/lib/pkgconfig` when `nythera-lib` is not already resolvable via system `pkg-config` or an explicit `PKG_CONFIG_PATH`.
- Follow the coordinated `0.1.x` rebuild discipline in `../nythera-lib/README.md`: build and install `nythera-lib` before building/testing `nythera-shell` against library changes; rebuild `nythera-shell` when `nythera-lib` public headers, inline/template code, or ABI-affecting build options change; restart running `nythera` (only when authorised) to load reinstalled `.cpp`-only library updates.

## Choose validation by change type

| Change                                                                                    | Default validation                                                                                                                                                                                                   |
| ----------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Documentation or comments only                                                            | `git diff --check`; run `just format-docs-check` when Markdown structure or syntax changes. No C++ build/tests.                                                                                                      |
| Pure UI styling, spacing, geometry or animations                                          | Incremental `just build` if C++ changed; visual inspection in the real shell only if an appropriate working session is available and it adds value. Do not run the entire test suite or fabricate mock visual tests. |
| Normal C++ behaviour changes                                                              | Incremental `just build`; run only directly related Meson tests using `meson test -C build-debug <test-name> --print-errorlogs`.                                                                                     |
| Configuration, persistence, parsing, security or nontrivial state transitions             | Incremental build and directly relevant tests; add a focused regression test when it protects a deterministic, meaningful contract not already covered.                                                              |
| Public headers, shared UI primitives, event loop, rendering backend or broad architecture | Incremental build plus a reasoned selection of affected tests; expand to full suite only when impact cannot be bounded.                                                                                              |
| Translation key additions or renames                                                      | `python3 tools/i18n-check.py` plus change-relevant checks. Edit the English catalogue only unless explicitly tasked otherwise.                                                                                       |
| Palette role/alpha changes                                                                | `just role-alpha-check` plus relevant checks.                                                                                                                                                                        |
| CI/workflow scripts                                                                       | `just test-workflows` when relevant; no C++ build unless build logic changes.                                                                                                                                        |

## Working with project documentation

- `ARCHITECTURE.md`: UI lifecycle, rendering, services, surfaces, and other implementation invariants; required for architectural/refactoring work.
- `CONTRIBUTING.md` and `BUILDING.md`: coding conventions, build/test guidance, and contributor process as relevant.
- `SCOPE.md`: distinguish approved scope, experiments, and future plans.
- `REBRAND.md`: Nythera-owned identity, external Noctalia contracts, plugin translations, and migration; required for naming/API changes.
- `docs/NYTHERA-CUSTOMISATIONS.md`: documented Nythera behaviours, development status, and compatibility risks. Check actual code before treating an item as implemented.
- `docs/UPSTREAM-INTEGRATION.md`: required procedure for integrating changes from Noctalia upstream. Do not use the standard narrow-change budget to justify skipping cross-subsystem compatibility checks for a broad update.
- When changing a documented behaviour or architectural contract, update its owning document without duplicating rules across files.

## Upstream integration

- Create a temporary task/integration branch (or separate worktree when warranted) for substantial or risky changes, especially upstream integrations.
- Treat upstream integration as a **semantic migration**, not just Git conflict resolution: evaluate changed APIs and indirect Nythera dependencies, including custom files absent from the upstream diff.
- Preserve Nythera's **intended behaviour**, not necessarily old code. Where upstream replaces an architecture, migrate customisation to the new interface instead of blindly retaining obsolete mechanisms.
- For the first Noctalia update, start from the verified shared-ancestor SHA in `docs/UPSTREAM-INTEGRATION.md` and verify it against freshly fetched history; no upstream merge has occurred since the fork. For later updates, track the last upstream target merged/imported and carry forward exclusions or deferred items. Never infer completeness from ahead/behind counts, a clean merge, or selective imports.
- Account for each relevant upstream change as incorporated, adapted, deliberately excluded, deferred, or blocked, with rationale. A clean Git merge and successful build alone do not prove compatibility.
- Follow `docs/UPSTREAM-INTEGRATION.md`, check impacted entries in `docs/NYTHERA-CUSTOMISATIONS.md`, and record actual validation plus unresolved work before declaring completion.
- GitHub CI currently runs on pull requests and pushes to `main`, not ordinary pushes to `dev`. Do not treat a successful `dev` push as proof that CI passed; report actual checks performed.
