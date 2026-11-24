# Stage 4.2 — Development Foundations Report

**Date:** 2026-11-24  
**Scope:** Findings **D2**, **D5**, and the **development-tooling portion of D12** from `audits-investigations/post-separation-development-readiness.md`  
**Branches modified:** `dev` across `nythera-lib`, `nythera-shell`, and `nythera-files` (all changes left uncommitted for review)

---

## 1. Summary of changes

Stage 4.2 establishes consistent development foundations across the separated **NytheraLib**, **Nythera Shell**, and **Nythera Files** repositories without altering application behaviour, dependencies, Polkit policies, CI workflows, or active `build-debug/` trees:

- **NytheraLib `justfile` (D2):** Created a lightweight `justfile` aligned with Shell and Files (`debug`, `release`, `asan` modes; `debugoptimized` default for `debug`; `--prefix "$HOME/.local/opt/nythera-lib" --libdir=lib`; `configure`, `build`, `install`, `uninstall`, `clean`, `rebuild`, `format`, `format-check`, `format-code`, `format-code-check`, `lint`, `fix`, with no test framework or `test` recipe).
- **Automatic `nythera-lib` `pkg-config` discovery (D2):** Updated `nythera-shell/justfile` and `nythera-files/justfile` with `_resolve-pkg-config-path` so `configure`, `build`, `_ensure-configured`, `test`, and `rebuild` automatically discover `$HOME/.local/opt/nythera-lib/lib/pkgconfig` when `nythera-lib` is not already resolvable via system `pkg-config` or an explicit `PKG_CONFIG_PATH`, without modifying Fish or global environment configuration.
- **Formatting and lint tooling parity (D12 development tooling):** Copied Shell's `.clang-format` to `nythera-lib` (matching `nythera-files`'s already-identical `.clang-format`), copied Shell's `.clang-tidy` to both `nythera-lib` and `nythera-files`, added `alias_target('generated-headers', _protocol_headers)` to `nythera-lib/meson.build`, and added `format`, `format-check`, `format-code`, `format-code-check`, `lint`, and `fix` recipes to `nythera-files/justfile` and `nythera-lib/justfile` scoped exclusively to each repository's own sources (`src` in `nythera-lib`; `src tests` in `nythera-files` and `nythera-shell`). No repository-wide reformatting or lint remediation was executed.
- **Repository-local `AGENTS.md` and rebuild discipline docs (D5, D12):** Added tailored `AGENTS.md` files in `nythera-lib` and `nythera-files`, added a concise NytheraLib cross-repository dependency subsection to `nythera-shell/AGENTS.md`, and documented the `0.1.x` shared-library build/install order, consumer rebuild vs restart rules, and stale-install hazards in `nythera-lib/README.md` (with matching workflow references in `nythera-shell/BUILDING.md` and `nythera-files/README.md`).

---

## 2. Files changed, grouped by repository

### `/mnt/Vegita/git-repos/nythera-lib` (`dev`)

- **Created `justfile`**: Standardised `configure`, `build`, `install`, `uninstall`, `clean`, `rebuild`, `format`, `format-check`, `format-code`, `format-code-check`, `lint`, and `fix` recipes targeting `$HOME/.local/opt/nythera-lib` with `libdir=lib` and `debugoptimized` default.
- **Created `.clang-format`**: Baseline C++ formatting configuration matching `nythera-shell/.clang-format`.
- **Created `.clang-tidy`**: Baseline `clang-tidy` checks and `HeaderFilterRegex` matching `nythera-shell/.clang-tidy`.
- **Created `AGENTS.md`**: Repository-specific agent instructions for NytheraLib (application-agnostic invariants, `just` workflow, `0.1.x` rebuild discipline, validation rules, Git safety).
- **Modified `meson.build`**: Added `alias_target('generated-headers', _protocol_headers)` after Wayland protocol header generation so `just lint` / `_clang_tidy` can generate protocol headers cleanly.
- **Modified `README.md`**: Documented the `just` workflow and added the **Coordinated development and rebuild discipline (`0.1.x`)** section.

### `/mnt/Vegita/git-repos/nythera-shell` (`dev`)

- **Modified `justfile`**: Added `nythera-lib-pkgconfig` variable and `_resolve-pkg-config-path` helper, and wired `PKG_CONFIG_PATH` resolution into `configure`, `build`, `_ensure-configured`, and `test`.
- **Modified `AGENTS.md`**: Added the **Cross-repository dependencies (NytheraLib)** subsection referencing automatic `pkg-config` discovery, `nythera-lib/README.md` rebuild rules, and Files independence.
- **Modified `BUILDING.md`**: Updated NytheraLib development setup instructions to reflect automatic user-local `pkg-config` discovery in `just`, explicit path precedence, and the coordinated rebuild discipline.
- **Created `audits-investigations/stage-4.2-development-foundations.md`**: This Stage 4.2 implementation and validation report.

### `/mnt/Vegita/git-repos/nythera-files` (`dev`)

- **Modified `justfile`**: Added `nythera-lib-pkgconfig` variable and `_resolve-pkg-config-path` helper wired into `configure`, `build`, `_ensure-configured`, and `test`; added `_check_formatter`, `format`, `format-check`, `format-code`, `format-code-check`, `_clang_tidy`, `lint`, and `fix` recipes targeting `src` and `tests`.
- **Created `.clang-tidy`**: Baseline `clang-tidy` checks and `HeaderFilterRegex` matching `nythera-shell/.clang-tidy`.
- **Created `AGENTS.md`**: Repository-specific agent instructions for Nythera Files (standalone invariants, Polkit safety boundaries, NytheraLib dependency/rebuild rules, validation matrix, full Test Isolation and Data Safety section).
- **Modified `README.md`**: Updated **Building & Installation** with `just` workflow, automatic user-local `nythera-lib` `pkg-config` discovery, helper prefix distinction (`/usr/local` vs Arch `PKGBUILD` `/usr`), and NytheraLib rebuild discipline.

---

## 3. Development workflow before and after

| Task | Before Stage 4.2 | After Stage 4.2 |
| --- | --- | --- |
| Build & install `nythera-lib` | Manual `meson setup`, `meson compile`, `meson install` with explicit `--prefix="$HOME/.local/opt/nythera-lib" --libdir=lib`; default `meson setup` selected `buildtype=debug`. | `just configure`, `just build`, `just install debug` (defaults to `debugoptimized`, `$HOME/.local/opt/nythera-lib`, `libdir=lib`). |
| Configure/build/test Shell & Files in a fresh terminal | Failed unless `PKG_CONFIG_PATH="$HOME/.local/opt/nythera-lib/lib/pkgconfig"` (or `-Dpkg_config_path=...`) was manually exported/passed. | `just configure`, `just build`, `just rebuild`, and `just test` automatically fall back to `$HOME/.local/opt/nythera-lib/lib/pkgconfig` while respecting system installs and explicit `PKG_CONFIG_PATH`. |
| Format & lint checks across repos | `.clang-format` existed in Shell and Files only; `.clang-tidy` and `format`/`lint` recipes existed in Shell only. | Identical `.clang-format` and `.clang-tidy` in all three repos; `just format`, `just format-check`, `just lint`, `just fix` available in all three repos targeting each repo's own files. |
| Agent guidance & library rebuild rules | `AGENTS.md` existed only in Shell; `0.1.x` ABI/rebuild expectations were only noted in Stage 4.1 report. | Dedicated `AGENTS.md` in all three repos plus clear `0.1.x` build/install order, rebuild vs restart rules, and stale-install risk guidance in `nythera-lib/README.md`. |

---

## 4. Build configuration findings and decisions

### Investigation of `debug` vs `debugoptimized`

Inspection of `build-debug/meson-private/cmd_line.txt`, `build-debug/meson-info/intro-buildoptions.json`, and `nythera-shell` Git history (`3d20f26c5`: `chore(build): use debug-optimised development builds`) established that the `buildtype` mismatch is **residual from raw `meson setup` commands run during Stage 3 separation**, not an intentional architectural split:

- **`nythera-shell` (`justfile` and `BUILDING.md`)** already standardised on `debug-buildtype := "debugoptimized"` in commit `3d20f26c5`, while retaining `build-debug/` as the daily-driver directory name. However, `nythera-shell/meson.build` does not hardcode `buildtype` in `default_options`, and `nythera-shell/build-debug` was recreated via direct `meson setup` on `2026-11-24` without `--buildtype=debugoptimized`, leaving it at Meson's default `debug` (`optimization=0`).
- **`nythera-lib`** had no `justfile` during Stage 3, so `meson setup` defaulted `nythera-lib/build-debug` to `debug` (`optimization=0`).
- **`nythera-files`** already inherited `debug-buildtype := "debugoptimized"` in its `justfile`, and `nythera-files/build-debug` is configured as `debugoptimized` (`optimization=2`).

### Decisions

1. **Standardised development default:** All three repositories use `debug-buildtype := "debugoptimized"` for `mode=debug`, preserving `asan` (`--buildtype=debug -Db_sanitize=address,undefined`) and `release` (`--buildtype=release -Db_lto=true`).
2. **Preserved compiler, linker, and hardening settings:** Verified all three `meson.build` files retain:
   - `-D_GLIBCXX_ASSERTIONS` (or `-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE` for libc++) whenever `buildtype != 'release'`
   - `b_ndebug=if-release` (and `b_ndebug=false` on test targets in Shell and Files)
   - `-fuse-ld=lld` (when supported and not GCC + LTO)
   - `-gsplit-dwarf`, `-gz=zstd`, and `-Wl,--compress-debug-sections=zstd` when `get_option('debug')` is true and `buildtype != 'release'`
3. **ABI compatibility note:** Optimisation level differences (`-O0` vs `-O2`) alone do not break C++ ABI compatibility between `debug` and `debugoptimized`. What alters ABI/runtime contracts is switching between `release` and non-release builds (`_GLIBCXX_ASSERTIONS` / `_LIBCPP_HARDENING_MODE` and `NDEBUG`), changing sanitizers (`asan`), or modifying public headers/inline templates.

### Existing build directories requiring reconfiguration (untouched)

Per Stage 4.2 instructions, existing `build-debug/` trees were **not** modified or reconfigured:

| Directory | Current `buildtype` | Status & action needed |
| --- | --- | --- |
| `nythera-lib/build-debug` | `debug` (`optimization=0`) | Needs non-destructive reconfiguration to `debugoptimized` (also picks up new `generated-headers` alias target and `compile_commands.json` symlink on next `just configure` / `just build`). |
| `nythera-shell/build-debug` | `debug` (`optimization=0`) | Needs non-destructive reconfiguration to `debugoptimized` (will occur automatically via `_ensure-configured` on next `just build`, or explicitly via `just configure`; `cmd_line.txt` currently caches `pkg_config_path = /home/kangy/.local/opt/nythera-lib/lib/pkgconfig`). |
| `nythera-files/build-debug` | `debugoptimized` (`optimization=2`) | Already `debugoptimized`; no `buildtype` change needed (still lacks root `compile_commands.json` symlink and caches `pkg_config_path` in `cmd_line.txt`). |

---

## 5. Dependency discovery behaviour and precedence

Both `nythera-shell/justfile` and `nythera-files/justfile` resolve `PKG_CONFIG_PATH` via `_resolve-pkg-config-path` inside `configure`, `build`, `_ensure-configured`, and `test` (with `rebuild` covered via `clean` + `build`) using this exact precedence order:

1. **System `pkg-config` or explicit `PKG_CONFIG_PATH` containing `nythera-lib.pc`:** If `pkg-config --exists nythera-lib` succeeds in the caller's environment, `PKG_CONFIG_PATH` is left untouched (empty if unset, or the caller's exact `PKG_CONFIG_PATH`). The user-local prefix is **not** injected, ensuring system packages or explicit overrides (including intentionally incompatible versions) are never silently bypassed or masked.
2. **Explicit `PKG_CONFIG_PATH` for other packages + user-local fallback:** If `pkg-config --exists nythera-lib` fails, `PKG_CONFIG_PATH` is non-empty, and `$HOME/.local/opt/nythera-lib/lib/pkgconfig/nythera-lib.pc` exists, `$HOME/.local/opt/nythera-lib/lib/pkgconfig` is **appended** (`"${PKG_CONFIG_PATH}:${local_pc_dir}"`) so explicit paths retain higher search priority.
3. **Fresh environment + user-local installation:** If `PKG_CONFIG_PATH` is unset/empty, `pkg-config --exists nythera-lib` fails, and `$HOME/.local/opt/nythera-lib/lib/pkgconfig/nythera-lib.pc` exists, `PKG_CONFIG_PATH` is exported as `$HOME/.local/opt/nythera-lib/lib/pkgconfig` for that recipe's process only.
4. **No `nythera-lib.pc` anywhere:** If neither system `pkg-config` nor `$HOME/.local/opt/nythera-lib/lib/pkgconfig/nythera-lib.pc` exists, `PKG_CONFIG_PATH` is left unchanged so Meson fails cleanly with a missing-dependency error.

*Note:* Existing `build-debug/` trees in `nythera-shell` and `nythera-files` currently record `pkg_config_path = /home/kangy/.local/opt/nythera-lib/lib/pkgconfig` inside `meson-private/cmd_line.txt`. If switching an already-configured directory to a system-installed `nythera-lib`, pass `meson setup --reconfigure --clearcache` once to clear the cached Meson option.

---

## 6. Validation results

All validation checks were non-destructive and verified against live command outputs without altering `build-debug/` or system state:

| Check | Result | Evidence |
| --- | --- | --- |
| Justfile parsing (`just --list`, `just --summary`, `just --evaluate`) | **Passed** | All three justfiles parsed cleanly; `mode=debug`, `debug-buildtype=debugoptimized`, `cpp-std=c++23` consistent across all three; `nythera-lib` sets `prefix=/home/kangy/.local/opt/nythera-lib`, `libdir=lib`; Shell and Files set `nythera-lib-pkgconfig=/home/kangy/.local/opt/nythera-lib/lib/pkgconfig`. |
| Invalid build-mode rejection (`configure`, `build`, `clean`, `install`, `uninstall`, `rebuild`, `lint`, `fix` with `invalid-mode`) | **Passed** | All tested recipes across all three repos exited with code 1 via `_validate-mode` (`error: invalid build mode 'invalid-mode' (expected: debug, release, or asan)`); no `build-invalid-mode` directory was created. |
| Fresh-environment `nythera-lib` discovery (`env -u PKG_CONFIG_PATH`) | **Passed** | `_resolve-pkg-config-path` in both Shell and Files returned `/home/kangy/.local/opt/nythera-lib/lib/pkgconfig`; `pkg-config --modversion nythera-lib` with that path returned `0.1.0`. |
| Explicit `PKG_CONFIG_PATH` precedence & incompatibility safety | **Passed** | Tested with temporary `mktemp -d` directories: (a) empty temp dir yielded `<tmp>:/home/kangy/.local/opt/nythera-lib/lib/pkgconfig` (explicit dir kept first); (b) temp dir containing `nythera-lib.pc` (`0.1.99`) yielded `<tmp>` only; (c) temp dir containing `nythera-lib.pc` (`0.2.0`) yielded `<tmp>` only (no silent fallback to user-local); (d) missing local `.pc` with empty env returned empty string. |
| `.clang-format` & `.clang-tidy` parity | **Passed** | Identical SHA-256 checksums across all three repos (`0fb5e61b...` for `.clang-format`; `113825c4...` for `.clang-tidy`). |
| Formatting/lint recipe scoping & tool check | **Passed** | `just _check_formatter` succeeded (`clang-format version 23.1.5` >= 22) in all three repos; `format-code`/`format-code-check` target `src` only in `nythera-lib` and `src tests` in Shell/Files; `_clang_tidy` uses `-header-filter='\.\./src/.*'` and `"^${src_root}/.*"`. |
| `meson.build` dev optimisations & `build-debug/` preservation | **Passed** | LLD, split DWARF, zstd debug compression, `b_ndebug=if-release`, and stdlib assertions remain intact in all three `meson.build` files; `intro-buildoptions.json` in all three `build-debug/` directories confirmed unmodified. |
| `git diff --check` | **Passed** | Exited 0 with no whitespace errors across all three repositories. |

---

## 7. Outstanding checks or actions requiring maintainer approval

1. **Reconfigure `nythera-lib/build-debug` and `nythera-shell/build-debug` to `debugoptimized`:**
   - In `nythera-lib`: run `just configure` (or `just build`, which runs `_ensure-configured`, followed by `just install debug` if testing updated binaries) to update `build-debug` from `debug` to `debugoptimized`, register the `generated-headers` alias target in Ninja, and create the `compile_commands.json` symlink.
   - In `nythera-shell`: run `just configure` (or `just build`) to switch `build-debug` from `debug` to `debugoptimized` and create `compile_commands.json`.
   - In `nythera-files` (optional convenience): run `just configure` if a root `compile_commands.json` symlink is desired; `build-debug` is already `debugoptimized`.
2. **End-to-end `just configure`/`just build`/`just lint` runtime verification:** Live Meson configure/compile, `just lint`, and `meson test` execution were intentionally deferred to avoid mutating active `build-debug/` trees or installing libraries during Stage 4.2.
3. **Commit and push review:** Review the uncommitted diffs and untracked files across all three `dev` branches before committing or pushing.
4. **Remaining Priority A/B items outside Stage 4.2:** Host Polkit reconciliation (**D1**, **D4**), `resolveFilesAssetsRoot()` executable-relative lookup (**D3**), Tier 1/Tier 2 isolated validation, and GitHub CI workflows (**D12** CI portion) remain deferred to their authorised stages.

---

## 8. Deviations from authorised scope

None. All work remained strictly within findings **D2**, **D5**, and the **development-tooling portion of D12** on the `dev` branches of the three repositories, with no application code changes, no dependency pruning, no test migration, no asset/Polkit/CI changes, no repo-wide reformatting, no build-tree reconfiguration, and no commits or pushes.

---

## 9. Git working-tree status

### `/mnt/Vegita/git-repos/nythera-lib` (`dev`)

```text
 M README.md
 M meson.build
?? .clang-format
?? .clang-tidy
?? AGENTS.md
?? justfile
```

### `/mnt/Vegita/git-repos/nythera-shell` (`dev`)

```text
 M AGENTS.md
 M BUILDING.md
 M justfile
?? audits-investigations/NYTHERA-REPOSITORY-SPLIT-STAGES-1-AND-2-SUMMARY.md
?? audits-investigations/NYTHERA-THREE-WAY-REPOSITORY-SPLIT-AUDIT.md
?? audits-investigations/post-separation-development-readiness.md
?? audits-investigations/stage-4.2-development-foundations.md
```

*(Note: the first three files under `audits-investigations/` were pre-existing untracked investigation reports present before Stage 4.2.)*

### `/mnt/Vegita/git-repos/nythera-files` (`dev`)

```text
 M README.md
 M justfile
?? .clang-tidy
?? AGENTS.md
```
