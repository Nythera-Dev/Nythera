# Stage 4.4 — Development Environment Reconciliation Report

**Date:** 2026-11-24  
**Branches active:** `dev` across `nythera-lib`, `nythera-shell`, and `nythera-files` (Stage 4.4 approved; committing & pushing on `dev`)  
**Scope:** Development installation convention docs (`D4`, `D11`), targeted Shell test fixes (`i18n_supported_languages`, `plugin_bindings`), `debugoptimized` build alignment, coordinated `nythera-lib` -> `nythera-shell` & `nythera-files` rebuilds and library install, Polkit reconciliation verification, full regression test runs, shared visual identity migration & visual regression remediation (Section 11), authorised application restarts, removal of the temporary compatibility bundle, and final live `en-GB` lock-screen acceptance.

> [!IMPORTANT]
> **Stage 4.4 is complete and approved.** Final manual `en-GB` lock-screen authentication has passed following removal of the temporary lock-screen compatibility bundle (`/mnt/Vegita/git-repos/nythera-shell/build-debug/assets`). Two separate outstanding items outside Stage 4.4 scope remain retained for follow-up: (1) Properties dialogue button flickering after applying permission changes in `nythera-file-manager`, and (2) the pre-existing unrelated `nythera:discord_template` test failure (`5e297b14f`).

---

## 1. Changes made across each repository

### `/mnt/Vegita/git-repos/nythera-lib` (`dev`)

- **Shared visual identity migration & palette fix (see Section 11):** Migrated shared branding SVGs/PNGs, diagonal header accent, and shared palettes (`Scarlet-Protocol.json` updated `"hover": "#301722"`, `Umbriel-Redline.json`, `README.md`) into `assets/`, switched `src/system/internal_app_metadata.cpp` to `paths::sharedAssetPath(...)`, and extended `tests/resource_paths_test.cpp`.
- Reconfigured `/mnt/Vegita/git-repos/nythera-lib/build-debug` non-destructively from `debug` to `debugoptimized`, compiled `libnythera_lib.so.0.1.0` and all 53 test targets, ran the full suite (`53/53` OK), and installed updated headers, library binaries, `nythera-lib.pc`, and shared assets (`share/nythera-lib/assets/`) into `/home/kangy/.local/opt/nythera-lib`.

### `/mnt/Vegita/git-repos/nythera-shell` (`dev`)

- **Modified `assets/translations/en.json`:** Added missing English action-command translations under `settings.widgets.actions.commands` for `restart` and `stop` (`label` and `description`). Non-English translation catalogues were not modified.
- **Modified `meson.build`:** Added `luau_dep` to `_nythera_test_dependencies` (line 794), preserving both `system_libraries=true` and `system_libraries=false` configurations, and updated hicolor desktop icon installation for renamed `assets/nythera.svg` (see Section 11).
- **Updated branding/accent consumers & removed duplicate shared assets (see Section 11):** Switched `launcher_panel.cpp`, `screen_wrap.cpp`, `setup_wizard_panel.cpp`, `polkit_panel.cpp`, and `tools/build-discord-template.py` to shared assets, renamed `assets/nythera-logo.svg` to `assets/nythera.svg`, and removed migrated runtime branding/palette copies.
- **Created `audits-investigations/stage-4.4-development-environment-reconciliation.md`:** This report.
- Reconfigured `/mnt/Vegita/git-repos/nythera-shell/build-debug` non-destructively from `debug` to `debugoptimized` and compiled `nythera` and all test targets (`296/296`) against the newly installed NytheraLib while preserving `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets`.

### `/mnt/Vegita/git-repos/nythera-files` (`dev`)

- **Modified `README.md`:** Documented the standardised development installation conventions (`/usr/local/libexec/` and `/usr/share/polkit-1/actions/` vs `packaging/arch/PKGBUILD` `/usr/lib/` for **D4**, `$HOME/.local/opt/nythera-lib` for NytheraLib, and unprivileged `--tags files,files-assets` and `DESTDIR` installs for **D11**).
- Rebuilt `nythera-file-manager`, `nythera-properties-helper`, `nythera-admin-helper`, generated `.policy` files, and all test targets against the updated NytheraLib inside the existing `debugoptimized` `/mnt/Vegita/git-repos/nythera-files/build-debug` directory.

---

## 2. Development installation conventions (`D4`, `D11`)

Documented under `## Building & Installation` -> `### Installation conventions (development, packaged, and unprivileged)` in `/mnt/Vegita/git-repos/nythera-files/README.md`:

- **NytheraLib development prefix:** `$HOME/.local/opt/nythera-lib` with `--libdir=lib` (`just install debug`), automatically discovered by Shell and Files `just` recipes when `nythera-lib` is not already in system `pkg-config` or an explicit `PKG_CONFIG_PATH`.
- **Nythera Files privileged development installation (`/usr/local`, D4):** Default `just configure` uses `--prefix /usr/local` (`libexecdir=libexec`), installing `nythera-admin-helper` and `nythera-properties-helper` under `/usr/local/libexec/` and Polkit policies under `/usr/share/polkit-1/actions/` (resolved from `polkit-gobject-1`'s `policydir`). Recommended helper-only install command:
  `sudo meson install --no-rebuild -C build-debug --tags admin-helper,properties-helper`
- **Package-managed installation (`packaging/arch/PKGBUILD`, D4):** `arch-meson` sets `--prefix=/usr --libexecdir=lib`, installing helpers under `/usr/lib/` while writing `dev.nythera.Files.*.policy` to the same `/usr/share/polkit-1/actions/` directory. Because helper paths are compiled into both the binaries and the policy XML, `/usr/local/libexec/` and `/usr/lib/` helper installations must never be mixed on the same host; obsolete helpers and policies from the unused prefix must be removed before switching.
- **Unprivileged user-prefix and staged installs (D11):**
  - Application and assets only (no root or Polkit policy writes):
    `just configure debug "$HOME/.local" && just build && meson install --no-rebuild -C build-debug --tags files,files-assets`
  - Complete staged inspection tree without mutating host directories:
    `DESTDIR=/path/to/stage meson install --no-rebuild -C build-debug`
  - Polkit root-ownership/ancestor checks (`verifyInstalledPolkitFile`) keep elevated Properties and Administrator Mode disabled in unprivileged or staged installs.

---

## 3. Targeted Shell test fixes

### `i18n_supported_languages`

- **Consumer inspected:** `src/shell/settings/settings_window_scene.cpp:69-71` (`translatedIpcActionField` looks up `settings.widgets.actions.commands.<command>.{label,description}`), `tests/i18n_supported_languages_test.cpp:40-47` (validates every entry in `nythera::cli::kMsgCmd.subcommands`), and `nythera-lib/src/cli/schema_msg.h:759-760` (`restart` and `stop` command descriptions).
- **Fix applied (`assets/translations/en.json` only, lines 4247–4250 and 4295–4298):**
  - `"restart"`: `"label": "Restart Nythera"`, `"description": "Restart the running Nythera process"`
  - `"stop"`: `"label": "Stop Nythera"`, `"description": "Stop the running Nythera process"`
- **Verification:** `python3 tools/i18n-check.py` passed (`OK: 1050 tr keys, 5 trp base keys, all present in app (3320) + shared (216) catalogs`), and `51/159 nythera:i18n_supported_languages` passed (`0.00s`).

### `plugin_bindings`

- **Fix applied (`meson.build:794`):** Added `luau_dep` alongside `_nythera_system_dependencies`, `_nythera_shell_only_dependencies`, and `polkit_gobject_dep` in `_nythera_test_dependencies`, matching `_nythera_shell_dependencies`.
- **Preserved configurations:** Both `-Dsystem_libraries=true` (`cpp.find_library('Luau.*')` with headers `luacode.h`, `lua.h`, `lualib.h`) and `-Dsystem_libraries=false` (bundled `third_party/luau/` static library) define `luau_dep`, so both build options remain intact.
- **Verification:** `plugin_bindings_test` compiled and linked cleanly (`[230/296]`) and `79/159 nythera:plugin_bindings` passed (`0.01s`).

---

## 4. Build configuration and coordinated rebuild results

### Development build options preserved

| Repository | `buildtype` before | `buildtype` after | `optimization` | `debug` | `b_ndebug` | Preserved flags (`build.ninja`) |
| --- | --- | --- | --- | --- | --- | --- |
| `nythera-lib` | `debug` | `debugoptimized` | `2` | `true` | `if-release` | `-D_GLIBCXX_ASSERTIONS`, `-gsplit-dwarf`, `-gz=zstd`, `-fuse-ld=lld`, `-Wl,--compress-debug-sections=zstd` |
| `nythera-shell` | `debug` | `debugoptimized` | `2` | `true` | `if-release` | `-D_GLIBCXX_ASSERTIONS`, `-gsplit-dwarf`, `-gz=zstd`, `-fuse-ld=lld`, `-Wl,--compress-debug-sections=zstd` |
| `nythera-files` | `debugoptimized` | `debugoptimized` | `2` | `true` | `if-release` | `-D_GLIBCXX_ASSERTIONS`, `-gsplit-dwarf`, `-gz=zstd`, `-fuse-ld=lld`, `-Wl,--compress-debug-sections=zstd` |

*Note:* Initial `just configure` in `nythera-lib` failed (`ERROR: Unknown option: "tests"`) because `build-debug/meson-private/coredata.dat` predated Stage 4.3 adding `tests` to `meson_options.txt`. Running `meson setup build-debug --reconfigure` refreshed `coredata.dat` non-destructively, after which `just configure` completed cleanly.

### Coordinated build & install sequence (strict 4-worker limit applied after initial `nythera-lib` test build)

1. **NytheraLib build & install:** `just build` compiled `[230/230]` (`libnythera_lib.so.0.1.0`); `just test` passed **53/53**; `just install debug` installed into `/home/kangy/.local/opt/nythera-lib`:
   - `lib/libnythera_lib.so.0.1.0` (`libnythera_lib.so.0`, `libnythera_lib.so`)
   - `lib/pkgconfig/nythera-lib.pc` (`assetsdir=${datadir}/nythera-lib/assets`)
   - Updated headers under `include/nythera-lib/` (verified `core/files/resource_paths.h`, `i18n/i18n_service.h`, `ui/dialogs/search_picker_popup.h`)
   - `share/nythera-lib/assets/fonts/` (4 files) and `share/nythera-lib/assets/translations/` (29 JSON files)
2. **Nythera Shell rebuild (`-j 4`):** `just configure` reconfigured `build-debug` to `debugoptimized`; `meson compile -C build-debug -j 4 nythera` linked `[453/453]`; `meson compile -C build-debug -j 4` built `[296/296]` test targets. `readelf -d build-debug/nythera` verified `RUNPATH: [/home/kangy/.local/opt/nythera-lib/lib/pkgconfig/../../lib]` and `[libnythera_lib.so.0]`.
3. **Nythera Files rebuild (`-j 4`):** Meson regenerated `build.ninja` (`Cleaning... 33 files`, removing old copied `files-assets/fonts/*` and `files-assets/translations/*`); `meson compile -C build-debug -j 4 nythera-file-manager nythera-properties-helper nythera-admin-helper` linked `[37/37]`; `meson compile -C build-debug -j 4` built `[43/43]` test targets. `readelf -d build-debug/nythera-file-manager` verified `RUNPATH: [/home/kangy/.local/opt/nythera-lib/lib/pkgconfig/../../lib]` and `[libnythera_lib.so.0]`.

*(Build/test environment note: the execution sandbox did not mount `/home/kangy/.local/opt/nythera-lib/include`, `lib/libnythera_lib.so*`, or `share/nythera-lib/`, so `just install debug` and Shell/Files compilation and runtime tests that read/load from `$HOME/.local/opt/nythera-lib` were executed unsandboxed after confirming sandbox failures first.)*

---

## 5. Polkit reconciliation results

### Host state verified after maintainer-executed privileged install (`/usr/local` convention)

Previously on the host, `/usr/local/libexec/nythera-admin-helper` was stale (`1336976` bytes, `2026-11-23`), while `/usr/local/libexec/nythera-properties-helper` and `/usr/share/polkit-1/actions/dev.nythera.Files.modify-properties.policy` were absent. Following manual execution of `sudo meson install --no-rebuild -C /mnt/Vegita/git-repos/nythera-files/build-debug --tags admin-helper,properties-helper`, all four files are verified byte-identical to `/mnt/Vegita/git-repos/nythera-files/build-debug/`:

| Artifact | Installed path, ownership & mode | Size (`bytes`) | Verified SHA-256 (installed == `build-debug/`) | Status |
| --- | --- | --- | --- | --- |
| `nythera-admin-helper` | `/usr/local/libexec/nythera-admin-helper` (`root:root`, `0755`) | `1352408` | `453143b84aa1c44b6bda98b6f1ccaa7f4b1fefe26da0cd93ce906a2831a8ad25` | **Reconciled & verified** |
| `nythera-properties-helper` | `/usr/local/libexec/nythera-properties-helper` (`root:root`, `0755`) | `248800` | `2c28de5ff7055614e3ef95f439bc932865abf359c6d463f7b15ae0b1c35a9e6e` | **Installed & verified** |
| `dev.nythera.Files.admin-session.policy` | `/usr/share/polkit-1/actions/dev.nythera.Files.admin-session.policy` (`root:root`, `0644`) | `776` | `7c497ea69cd1a6dcdeb70a1c20e4400fd33609c8d1e69836777e71025eee3a31` | **Verified** (`HELPER_PATH=/usr/local/libexec/nythera-admin-helper`) |
| `dev.nythera.Files.modify-properties.policy` | `/usr/share/polkit-1/actions/dev.nythera.Files.modify-properties.policy` (`root:root`, `0644`) | `773` | `91c44498b4a208a4c49cbc5f7735573c7168eb88a64e645260d9621b1a1b7deb` | **Installed & verified** (`HELPER_PATH=/usr/local/libexec/nythera-properties-helper`) |
| `/usr/lib/nythera*` | None present | N/A | N/A | **Clean** — zero Arch package prefix conflicts |

- Verified `build-debug/admin_helper_paths.h` (`NYTHERA_ADMIN_HELPER_PATH "/usr/local/libexec/nythera-admin-helper"`, `NYTHERA_ADMIN_POLICY_PATH "/usr/share/polkit-1/actions/dev.nythera.Files.admin-session.policy"`) and `build-debug/property_helper_paths.h` (`NYTHERA_PROPERTIES_HELPER_PATH "/usr/local/libexec/nythera-properties-helper"`, `NYTHERA_PROPERTIES_POLICY_PATH "/usr/share/polkit-1/actions/dev.nythera.Files.modify-properties.policy"`).

---

## 6. Full regression test results

| Suite | Command & scope | Result | Specific Stage 4.3 / 4.4 targets & notes |
| --- | --- | --- | --- |
| **NytheraLib** | `just test` (53 tests) | **53 / 53 OK** (`0` fail) | Includes all 51 migrated tests + `resource_paths` + `i18n_catalog`. |
| **Nythera Shell** | `meson test -C build-debug --no-rebuild --num-processes 4 --print-errorlogs` (159 tests) | **158 / 159 OK** (`1` fail) | `51/159 nythera:i18n_supported_languages` **OK** (`0.00s`); `79/159 nythera:plugin_bindings` **OK** (`0.01s`). Single failure: `153/159 nythera:discord_template` (`0.29s`, exit 1) — pre-existing unrelated mismatch explained below. |
| **Nythera Files** (sandboxed check) | `meson test -C build-debug --no-rebuild --num-processes 4 --print-errorlogs` | `19 / 34 OK`, `15 fail` | 13 exit 127 (`libnythera_lib.so.0` hidden in sandbox), `file_transfer_pause` (`/var/tmp` read-only), `file_operation_manager` (`lchown` supplementary GID unmapped in user-ns). |
| **Nythera Files** (unsandboxed after verifying `ScopedTestEnv` isolation) | `meson test -C build-debug --no-rebuild --num-processes 4 --print-errorlogs` | **34 / 34 OK** (`0` fail) | `27/34 nythera-files:file_transfer_pause` **OK** (`0.64s`); `33/34 nythera-files:file_operation_manager` **OK** (`1.87s`); all 34 tests pass cleanly on isolated disposable fixtures. |

### Investigation of `153/159 nythera:discord_template` failure

- **Failure output:** `tests/discord_template_test.py:65` (`test_offline_bundle_is_current`): `Discord template is stale; run python3 tools/build-discord-template.py`.
- **Root cause:** Pre-existing and unrelated to Stage 4.1–4.4. Commit `5e297b14f` (`Remove UI labels`, 2026-11-13) edited `--panel-labels: on;` -> `--panel-labels: off;` directly inside the generated `assets/templates/discord/nythera.theme.css` (line 2418) rather than modifying `assets/templates/discord/vendor/system24.css` or `nythera.css`, leaving a 1-line diff against `tools/build-discord-template.py` (`assemble()`). Left untouched per Stage 4.4 scope restrictions ("No new features or unrelated bug fixes").

---

## 7. Runtime acceptance results

### Automated non-destructive diagnostic (pre-restart)

- Executed `./build-debug/nythera pam-helper nythera en-GB </dev/null` on the newly built `nythera` executable:
  - Logged `21:22:00.493 [INF] [i18n] language: en-GB` with zero asset-bundle or English-fallback warnings.
  - Wrote `0` bytes to stdout and exited with status `2` on stdin EOF (before reading a password or invoking PAM).

### Authorised application restarts and startup diagnostics (initial Phase 1; superseded by Section 11.8)

1. **Unlocked session verification:** Confirmed via `loginctl` (`LockedHint=no` across sessions `1`, `4`, `5`; Wayland session `4` `Active=yes`, `State=active`) and `nythera msg status` (`"locked": false`) before restarting Shell.
2. **Nythera Shell restart:** Executed `/mnt/Vegita/git-repos/nythera-shell/build-debug/nythera restart` (`restarting Nythera`), triggering an in-place `execv` restart of PID `2841` at `21:43:48`:
   - `/proc/2841/exe` resolves cleanly to `/mnt/Vegita/git-repos/nythera-shell/build-debug/nythera` (no `(deleted)` marker).
   - `/proc/2841/maps` maps `/home/kangy/.local/opt/nythera-lib/lib/libnythera_lib.so.0.1.0` cleanly.
   - `nythera msg status` reports `{"barVisible": true, "panelOpen": false, "activePanelId": null, "locked": false}`.
   - User journal (`21:43:48–21:43:51`) confirms clean startup: `nythera v0.1.0 (v0.1.0-9-g6f95cb58c227-dirty)`, `[INF] [i18n] language: en-GB`, Wayland `outputs=4`, EGL 1.5 initialized, `[INF] [polkit] registered Polkit authentication agent at /org/nythera/PolkitAuthenticationAgent`, resident wallpaper backing across all 4 outputs, WirePlumber ready, and zero asset, font, glyph, or translation errors.
3. **Nythera Files restart:** Sent `SIGTERM` to previous PID `465346` (exited cleanly) and launched `/mnt/Vegita/git-repos/nythera-files/build-debug/nythera-file-manager /home/kangy` detached as PID `1356690` (`21:44:49`):
   - `/proc/1356690/exe` resolves cleanly to `/mnt/Vegita/git-repos/nythera-files/build-debug/nythera-file-manager`.
   - `/proc/1356690/maps` maps `/home/kangy/.local/opt/nythera-lib/lib/libnythera_lib.so.0.1.0` cleanly.
   - Startup log (`/tmp/nythera-files-restart.log`) confirms `[INF] [i18n] language: en-GB`, keyboard/pointer/text-input-v3 bound, EGL 1.5 / OpenGL ES 3.2 NVIDIA initialized, 4 thumbnail decode workers spawned, and zero asset, font, glyph, i18n, or Polkit errors.

### Manual runtime acceptance checks

1. **Completed & passed (maintainer-verified):** Shared fonts (`NytheraDisplay-Regular.ttf`, `nythera-tabler.ttf`), Tabler glyphs (`tabler.json`), shared/app translations, app-specific assets, launcher branding glyph & diagonal header accent, `#301722` hover tint, Administrator Mode, Privileged Properties, and initial `en-GB` lock-screen unlock.
   - *Separate UI bug observed (out of Stage 4.4 scope):* Properties dialogue button flickering after applying permission changes in `nythera-file-manager` (recorded in Section 9; not modified during Stage 4.4).
2. **Final lock-screen check completed & passed (maintainer-verified):** Repeated live **lock-screen authentication using `en-GB`** following removal of `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets`; unlock succeeded cleanly.

---

## 8. Lock-screen recovery and compatibility bundle status

- **Deployment mismatch summary:** After Stage 3/4.3 moved shared fonts (`nythera-tabler.ttf`, `NytheraDisplay-Regular.ttf`) and shared translations from `nythera-shell/assets/` to `nythera-lib/assets/`, the running Shell and old installed `/home/kangy/.local/opt/nythera-lib/lib/libnythera_lib.so.0.1.0` still used the pre-migration `isAssetRoot()` validator requiring `fonts/nythera-tabler.ttf` inside Shell's asset root. When `PamAuthenticator::authenticateCurrentUser()` re-executed `/proc/self/exe` in `pam-helper` mode, the helper failed to validate `nythera-shell/assets`, fell back to English instead of `en-GB`, and exited with status `2` on the helper language guard before `authenticateDirect()`.
- **Temporary compatibility bundle removed (authorised):**
  - Removed only the 4 recovery symlinks (`emoji.json`, `fonts`, `templates`, `translations`) and removed the now-empty `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets` directory via `rm` + `rmdir` (`exit 0`).
  - Confirmed absence via `test ! -e /mnt/Vegita/git-repos/nythera-shell/build-debug/assets` (`exit 0`).
- **Post-removal verification (without compatibility bundle):**
  - Executed `/mnt/Vegita/git-repos/nythera-shell/build-debug/nythera pam-helper nythera en-GB </dev/null`: logged `00:11:19.479 [INF] [i18n] language: en-GB` with zero asset-bundle or English-fallback warnings, and exited with status `2` on stdin EOF (before reading a password or invoking PAM).
  - Re-ran asset and i18n tests without `build-debug/assets`: `nythera:i18n_supported_languages` (`1/1 OK`, `0.01s`), `nythera:firefox_theme_manifest` / `nythera:lockscreen_session_actions` / `nythera:template_apply_notify` (`3/3 OK`), and `nythera-lib:resource_paths` / `nythera-lib:i18n_catalog` (`2/2 OK`).
- **Final confirmation passed:** Maintainer verified live `en-GB` lock-screen authentication succeeded without `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets`; lock-screen recovery is permanently resolved.

---

## 9. Retained outstanding issues (out of Stage 4.4 scope)

**Stage 4.4 is approved and complete.** All Stage 4.4 installation, rebuild, Polkit reconciliation, visual identity migration, application restart, compatibility bundle removal, and final live `en-GB` lock-screen verification items have passed. The following pre-existing/separate issues remain retained outside Stage 4.4 for future follow-up:

1. **Separate outstanding UI bug:** In `nythera-file-manager`, the **Properties dialogue button flickers after applying permission changes**. Recorded for separate follow-up; not modified during Stage 4.4.
2. **Pre-existing unrelated test failure:** `nythera:discord_template` fails `tools/build-discord-template.py --check` due to commit `5e297b14f` (`--panel-labels: off;` edited directly in `assets/templates/discord/nythera.theme.css`).

---

## 10. Git working-tree status across all three repositories

### `/mnt/Vegita/git-repos/nythera-lib` (`dev`)

```text
 M src/system/internal_app_metadata.cpp
 M tests/resource_paths_test.cpp
?? assets/nythera-glyph-simple-vector.svg
?? assets/nythera-glyph-vector.svg
?? assets/nythera-header-accent.svg
?? assets/nythera-logo-flat.svg
?? assets/nythera-logo.png
?? assets/nythera-logo.svg
?? assets/palettes/
```

### `/mnt/Vegita/git-repos/nythera-shell` (`dev`)

```text
 D assets/nythera-glyph-simple-vector.svg
 D assets/nythera-glyph-vector.svg
 D assets/nythera-header-accent.svg
 D assets/nythera-logo-flat.svg
 D assets/nythera-logo.png
 D assets/nythera-logo.svg
 D assets/palettes/README.md
 D assets/palettes/Scarlet-Protocol.json
 D assets/palettes/Umbriel-Redline.json
 M assets/translations/en.json
 M meson.build
 M src/shell/bar/screen_wrap.cpp
 M src/shell/launcher/launcher_panel.cpp
 M src/shell/polkit/polkit_panel.cpp
 M src/shell/setup_wizard/setup_wizard_panel.cpp
 M tools/build-discord-template.py
?? assets/nythera.svg
```

*(Note: `audits-investigations/stage-4.4-development-environment-reconciliation.md` is ignored by `.gitignore:26`.)*

### `/mnt/Vegita/git-repos/nythera-files` (`dev`)

```text
 M README.md
```

---

## 11. Visual regression remediation and shared visual identity

### 11.1 Root cause of each visual regression

1. **Missing Nythera branding glyph in the Shell launcher (`nythera-logo.png`):**
   - `LauncherPanel::doLayout()` (`src/shell/launcher/launcher_panel.cpp:2383`) resolved `nythera-logo.png` via `paths::assetPath("nythera-logo.png")`.
   - `resolveShellAssetsRoot()` in `nythera-lib/src/core/files/resource_paths.cpp` searches `<exeDir>/assets` first (`build-debug/assets`), and `isAssetRoot()` only requires `emoji.json`, `templates/builtin.toml`, and `translations/en.json`.
   - Because the preserved lock-screen recovery bundle `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets` contains symlinks only for `emoji.json`, `fonts`, `templates`, and `translations`, `resolveShellAssetsRoot()` selected `build-debug/assets` instead of `nythera-shell/assets/`, causing `paths::assetPath("nythera-logo.png")` to point to a nonexistent file.
2. **Missing diagonal header accent in the Shell launcher (`nythera-header-accent.svg`):**
   - Exact same cause: `LauncherPanel::doLayout()` (`src/shell/launcher/launcher_panel.cpp:2406`) loaded `paths::assetPath("nythera-header-accent.svg")`, which resolved inside the incomplete `build-debug/assets` recovery bundle rather than `nythera-shell/assets/` or `nythera-lib/assets/`.
3. **Hover background colour (`#282027` brownish-purple in both Shell and Files):**
   - Full source comparison of `nythera-lib/src/`, `nythera-shell/src/`, and `nythera-files/src/` against pre-separation commit `f68a48057~1` verified **zero** changes to `Palette`, `colorForRole(ColorRole::Hover)`, `FixedPalette`, `expandFixedPaletteMode()`, `mapGeneratedPaletteMode()`, `Button::defaultPalette()`, `FileEntryRow`, `FileEntryTile`, `ThemeService`, or opacity/blending calculations. ELF symbol inspection (`readelf -Ws` / `readelf -Wr`) confirmed both consumers resolve `palette`, `setPalette`, and `colorForRole` cleanly from `libnythera_lib.so.0.1.0`.
   - Live configuration inspection confirmed `[theme]` in `/home/kangy/.local/state/nythera/settings.toml` (and `nythera msg color-scheme-get`) uses `custom Scarlet-Protocol` (`/home/kangy/.config/nythera/palettes/Scarlet-Protocol.json`, byte-identical to repo `assets/palettes/Scarlet-Protocol.json` added in commit `73b23f43e`).
   - `Scarlet-Protocol.json` previously defined `"hover": "#282027"` (`rgb(40, 32, 39)`), whereas the maintainer's reference screenshot of the prior appearance sampled to `#301722` (`rgb(48, 23, 34)` over `#101116` `surface`). Per explicit maintainer direction, `"hover"` was updated from `"#282027"` to `"#301722"`.

### 11.2 Corrections implemented

- Centralised shared Nythera visual identity assets (logos, glyphs, diagonal header accent, shared palettes) in `nythera-lib/assets/`.
- Updated `"hover"` in `nythera-lib/assets/palettes/Scarlet-Protocol.json` and `/home/kangy/.config/nythera/palettes/Scarlet-Protocol.json` from `"#282027"` to `"#301722"` (requested by maintainer).
- Updated all runtime consumers (`LauncherPanel`, `ScreenWrap`, `SetupWizardPanel`, `PolkitPanel`, and `nythera-lib`'s `internal_app_metadata.cpp`) to load branding and header accent files via `paths::sharedAssetPath(...)`, eliminating reliance on `paths::assetPath(...)` (and thus isolating them from the temporary `build-debug/assets` compatibility bundle as well as enabling standalone future apps to use shared branding).
- Renamed `nythera-shell/assets/nythera-logo.svg` to `nythera-shell/assets/nythera.svg` for Shell's hicolor desktop icon installation only (excluded from `<datadir>/nythera/assets/`), and removed all redundant shared branding and palette runtime files from `nythera-shell/assets/`.
- Updated `nythera-shell/tools/build-discord-template.py` to resolve `nythera-logo-flat.svg` and `nythera-header-accent.svg` from shared assets (`NYTHERA_LIB_ASSETS_DIR`, `pkg-config --variable=assetsdir nythera-lib`, or sibling `../nythera-lib/assets/`).
- Added a deterministic regression check in `nythera-lib/tests/resource_paths_test.cpp` verifying `paths::sharedAssetPath(...)` resolves all migrated branding, glyph, header accent, and palette files.

### 11.3 Shared asset and component ownership before and after

| Asset / component category | Ownership before | Ownership after |
| --- | --- | --- |
| Nythera Display font & Tabler glyph font/metadata (`NytheraDisplay-Regular.ttf`, `nythera-tabler.ttf`, `tabler.json`, `tabler-icons-license.txt`) | `nythera-lib/assets/fonts/` (Stage 4.3) | `nythera-lib/assets/fonts/` (unchanged) |
| Shared component translations (`translations/*.json`, 216 keys) | `nythera-lib/assets/translations/` (Stage 4.3) | `nythera-lib/assets/translations/` (unchanged) |
| Nythera logos & runtime branding (`nythera-logo.svg`, `nythera-logo.png`, `nythera-logo-flat.svg`) | `nythera-shell/assets/` | **`nythera-lib/assets/`** |
| Nythera glyphs (`nythera-glyph-vector.svg`, `nythera-glyph-simple-vector.svg`) | `nythera-shell/assets/` | **`nythera-lib/assets/`** |
| Diagonal header accent (`nythera-header-accent.svg`) | `nythera-shell/assets/` | **`nythera-lib/assets/`** |
| Shared palette resources (`Scarlet-Protocol.json`, `Umbriel-Redline.json`, `palettes/README.md`) | `nythera-shell/assets/palettes/` | **`nythera-lib/assets/palettes/`** |
| Palette engine & theme service (`builtin_palettes.*`, `fixed_palette*`, `palette.*`, `theme_service.*`) | `nythera-lib/src/{theme,ui}/` | `nythera-lib/src/{theme,ui}/` (unchanged) |
| Nebula Haze & background effects (`SurfaceHaze`, `SurfaceBlurRegion`, `SurfaceBackground`) | `nythera-lib/src/{render,ui}/` | `nythera-lib/src/{render,ui}/` (unchanged) |
| Reusable corner & dialog components (`ScreenCorner`, `DialogBackgroundArtwork`, `Button`, `FileEntryRow`, `FileEntryTile`) | `nythera-lib/src/ui/` | `nythera-lib/src/ui/` (unchanged) |

### 11.4 Assets and components retained by individual applications

- **Nythera Shell (`nythera-shell`):**
  - Shell wallpapers (`nythera-wallpaper.jpeg`, `nythera-wallpaper-ultrawide.jpeg`), desktop entries (`dev.nythera.Nythera.desktop`, `dev.nythera.Nythera.Annotator.desktop`), and hicolor desktop icon (`assets/nythera.svg`, installed only to `share/icons/hicolor/scalable/apps/nythera.svg`).
  - Shell-specific runtime data/templates (`emoji.json`, `firefox_theme/`, `templates/`, `translations/`).
  - Shell layout/config helpers (`bar_corner_shape.h`, `screen_corners.*`, `panel_manager.*`, `launcher_panel.*`).
  - Non-runtime promotional/org/concept/dev files (`noctalia.svg`, `nythera-org-avatar.png`, `nythera-square-logo.png`, `Nythera Neon Nebula Emblem.png`, `nythera-cleaned.xcf`, `concepts/`, `palettes/exports/`, and pre-existing `assets/fonts/` backups).
- **Nythera Files (`nythera-files`):**
  - Files-specific dialogue artwork (`files-assets/dialogue-window-artwork/{top-left-diagonals.svg,bottom-right-contour.svg}`), office document creation templates (`files-assets/templates/filemanager/office/*`), application icon concept (`files-assets/concepts/nythera-files-icon.png`), `LICENSE`, and Files application UI/operations (`src/filemanager/*`).

### 11.5 Files changed across each repository

- **`nythera-lib`:**
  - Added `assets/nythera-glyph-simple-vector.svg`, `assets/nythera-glyph-vector.svg`, `assets/nythera-header-accent.svg`, `assets/nythera-logo-flat.svg`, `assets/nythera-logo.png`, `assets/nythera-logo.svg`, `assets/palettes/{README.md,Scarlet-Protocol.json,Umbriel-Redline.json}`.
  - Modified `src/system/internal_app_metadata.cpp` (`paths::sharedAssetPath`), `tests/resource_paths_test.cpp` (added runtime asset assertions).
- **`nythera-shell`:**
  - Removed shared runtime copies `assets/nythera-glyph-simple-vector.svg`, `assets/nythera-glyph-vector.svg`, `assets/nythera-header-accent.svg`, `assets/nythera-logo-flat.svg`, `assets/nythera-logo.png`, `assets/palettes/{README.md,Scarlet-Protocol.json,Umbriel-Redline.json}`.
  - Renamed `assets/nythera-logo.svg` -> `assets/nythera.svg`.
  - Modified `meson.build` (excluded `nythera.svg` from `install_subdir('assets', ...)`, updated hicolor `install_data('assets/nythera.svg', ...)`).
  - Modified `src/shell/launcher/launcher_panel.cpp`, `src/shell/bar/screen_wrap.cpp`, `src/shell/setup_wizard/setup_wizard_panel.cpp`, `src/shell/polkit/polkit_panel.cpp` (switched to `paths::sharedAssetPath(...)`).
  - Modified `tools/build-discord-template.py` (added `shared_assets_dir()` discovery).
- **`nythera-files`:** No source or asset changes required (`README.md` update from earlier in Stage 4.4 preserved).

### 11.6 Resource resolution and installation changes

- `nythera-lib/meson.build` (`install_subdir('assets', install_dir: get_option('datadir') / 'nythera-lib')`) automatically installs all newly migrated SVGs, PNGs, and `palettes/` into `<prefix>/share/nythera-lib/assets/`.
- `paths::sharedAssetPath(...)` resolves `NYTHERA_LIB_ASSETS_DIR`, `dladdr`-derived library-relative `<libDir>/../share/nythera-lib/assets` (and multiarch `../../share/nythera-lib/assets` and dev `<repo>/assets`), and compile-time `NYTHERA_LIB_INSTALL_PREFIX` / `NYTHERA_LIB_INSTALL_DATADIR` — functioning identically in both development builds and installed/relocated trees, independently of `nythera-shell/build-debug/assets`.
- `nythera-shell/meson.build` no longer installs shared branding or shared palettes into `share/nythera/assets/`, while continuing to install `nythera.svg` solely into `share/icons/hicolor/scalable/apps/nythera.svg`.

### 11.7 Validation results

- **`nythera-lib` (`-j 4` / `--num-processes 4`):** Built cleanly and passed **53 / 53** tests (`0` fail), including updated `resource_paths` and `surface_haze_render`.
- **Staged `DESTDIR` install (`nythera-lib`):** Verified `share/nythera-lib/assets/` contains `fonts/`, `translations/`, all 6 branding/glyph/accent assets, and `palettes/{README.md,Scarlet-Protocol.json,Umbriel-Redline.json}`.
- **`nythera-shell` (`-j 4` / `--num-processes 4`):** Compiled `nythera`, `i18n_supported_languages_test`, `plugin_bindings_test`, and `surface_haze_test` cleanly; targeted tests passed **3 / 3** (`i18n_supported_languages`, `plugin_bindings`, `surface_haze`).
- **Staged `DESTDIR` install (`nythera-shell`):** Verified `share/icons/hicolor/scalable/apps/nythera.svg` is installed and `share/nythera/assets/` contains zero shared branding or palette JSON files (`palettes/` contains only `exports/`).
- **`tools/build-discord-template.py`:** Verified `assemble()` runs cleanly with `NYTHERA_LIB_ASSETS_DIR=/mnt/Vegita/git-repos/nythera-lib/assets` (`1114311` bytes).
- **Source, palette JSON, & formatting checks:** `rg` confirmed zero remaining `paths::assetPath("nythera-(logo|header-accent|glyph)...")` references across all three repos; `python3 -m json.tool` and `diff -u` verified `nythera-lib/assets/palettes/Scarlet-Protocol.json` and `/home/kangy/.config/nythera/palettes/Scarlet-Protocol.json` match (`"hover": "#301722"`); `fixed_palette_json` and `resource_paths` passed (`2/2 OK`); touched C++ files/lines passed `clang-format --dry-run -Werror`; `git diff --check` exited `0` across all three repos; `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets` remains untouched.

### 11.8 Installation, restart & runtime diagnostics

1. **NytheraLib installation (`/home/kangy/.local/opt/nythera-lib`):** Executed `just install debug` in `/mnt/Vegita/git-repos/nythera-lib` (`exit 0`). Verified `/home/kangy/.local/opt/nythera-lib/lib/libnythera_lib.so.0.1.0` (`25536112` bytes), all 6 migrated branding/accent files (`nythera-glyph-simple-vector.svg`, `nythera-glyph-vector.svg`, `nythera-header-accent.svg`, `nythera-logo-flat.svg`, `nythera-logo.png`, `nythera-logo.svg`), and `/home/kangy/.local/opt/nythera-lib/share/nythera-lib/assets/palettes/Scarlet-Protocol.json` (`"hover": "#301722"`).
2. **Pre-restart unlock verification:** `loginctl show-session 4` reported `Active=yes`, `State=active`, `LockedHint=no` (session `5`: `LockedHint=no`), and `nythera msg status` reported `"locked": false`.
3. **Application restarts:**
   - Executed `/mnt/Vegita/git-repos/nythera-shell/build-debug/nythera restart` (`exit 0`, output `restarting Nythera`), performing an in-place `execv` on `PID 2841` (shutdown `23:02:25.894`, fresh startup `23:02:26.000`).
   - Sent `SIGTERM` to the active `nythera-file-manager` instance (`PID 1413887`), verified exit via `pgrep -fa nythera`, and launched `/mnt/Vegita/git-repos/nythera-files/build-debug/nythera-file-manager /home/kangy` (`PID 1429251`, startup `23:03:20.286`).
4. **Post-restart runtime verification:**
   - `/proc/2841/exe` -> `/mnt/Vegita/git-repos/nythera-shell/build-debug/nythera` and `/proc/1429251/exe` -> `/mnt/Vegita/git-repos/nythera-files/build-debug/nythera-file-manager` (no `(deleted)` binaries).
   - Both `/proc/2841/maps` and `/proc/1429251/maps` map `/home/kangy/.local/opt/nythera-lib/lib/libnythera_lib.so.0.1.0` (`inode 53065396`, no `(deleted)`), `NytheraDisplay-Regular.ttf` (`inode 53065351`), and `nythera-tabler.ttf` (`inode 53065352`).
   - `nythera msg status` returned `{"barVisible": true, "panelOpen": false, "activePanelId": null, "locked": false}`; `nythera msg color-scheme-get` returned `custom Scarlet-Protocol`.
   - `journalctl --user --since "2026-11-24 23:01:58" --no-pager` showed clean `umbriel[2841]` startup (`[i18n] language: en-GB`, `loaded Nythera corner logo (60px)` across all 4 outputs, active Polkit agent, zero `[ERR]` entries, and zero asset/font/glyph/palette warnings); `/tmp/nythera-files-restart.log` confirmed clean `nythera-file-manager` initialisation (`[i18n] language: en-GB`, EGL/OpenGL ES init, zero errors).
   - `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets` remains intact with all 4 temporary compatibility symlinks (`emoji.json`, `fonts`, `templates`, `translations`).

### 11.9 Manual acceptance, post-removal status & completion

1. **Manual runtime acceptance:** Passed (launcher branding glyph, diagonal header accent, `#301722` hover tint in Shell and Files, shared visual identity assets, Administrator Mode, Privileged Properties, and initial `en-GB` lock-screen unlock).
2. **Compatibility bundle cleanup:** Removed only the 4 recovery symlinks (`emoji.json`, `fonts`, `templates`, `translations`) and empty directory `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets`; verified normal asset resolution and `en-GB` PAM helper initialisation (`00:11:19.479 [INF] [i18n] language: en-GB`, zero warnings, exit `2` on stdin EOF) without the compatibility bundle.
3. **Final lock-screen test passed:** Maintainer verified live `en-GB` lock-screen authentication succeeded without `/mnt/Vegita/git-repos/nythera-shell/build-debug/assets`. **Stage 4.4 is complete and approved.**

### 11.10 Retained outstanding issues & out-of-scope items

- **Separate outstanding UI bug (out of Stage 4.4 scope):** Properties dialogue button flickering after applying permission changes in `nythera-file-manager` (observed during manual acceptance; recorded for separate follow-up, not modified during Stage 4.4).
- **Pre-existing unrelated test failure:** `nythera:discord_template` fails `tools/build-discord-template.py --check` due to commit `5e297b14f` (`--panel-labels: off;` edited directly in `assets/templates/discord/nythera.theme.css`).
- **Pre-existing non-runtime `assets/fonts/` backup files in Shell:** `Font Overview.png`, `NytheraDisplay-New/`, and `nythera-tabler.ttf.bak` remain in `nythera-shell/assets/fonts/` (`D15`, out of Stage 4.4 scope).
