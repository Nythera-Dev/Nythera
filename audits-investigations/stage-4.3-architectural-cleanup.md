# Stage 4.3 — Post-Separation Architectural Cleanup Report

**Date:** 2026-11-24  
**Branches modified:** `dev` across `nythera-lib`, `nythera-shell`, and `nythera-files` (all changes left uncommitted for review)  
**Scope:** Findings **D3**, **D6**, **D7**, **D8**, **D9**, the **GVfs portion of D14** from `audits-investigations/post-separation-development-readiness.md`, and cross-repository `AGENTS.md` / documentation alignment.

---

## 1. Executive summary

Stage 4.3 completes the post-separation architectural cleanup across **NytheraLib**, **Nythera Shell**, and **Nythera Files** without altering active `build-debug/` trees, `$HOME/.local/opt/nythera-lib`, host Polkit files, copyright/attribution, or Git history:

- **NytheraLib test ownership (D9):** Migrated **51** library-owned unit tests (plus `test_check.h` and `test_isolation.h`) from `nythera-shell` to `nythera-lib`, added **2** new regression tests (`resource_paths`, `i18n_catalog`), wired Meson test targets and `-Dtests` option, added `just test` and `tests` coverage in formatting/lint recipes, disabled tests in `nythera-lib`'s Arch `PKGBUILD`, and removed the 51 migrated tests from `nythera-shell`. Retained **7** tests whose headers reside in `nythera-lib` but whose tested implementations live in `nythera-shell/src/`.
- **Dependency ownership cleanup (D6) & optional GVfs metadata (D14):** Pruned **19** redundant `nythera-lib`-private dependencies and the unused `NYTHERA_USE_JEMALLOC` define from `nythera-files/meson.build` (and **12** private runtime dependencies from `nythera-files/packaging/arch/PKGBUILD`), pruned **14** redundant private dependencies from `nythera-shell/meson.build` (**7** runtime deps + `stb` makedep from `nythera-shell/packaging/arch/PKGBUILD`), while preserving `curl_dep`/`curl` in Shell (verified direct `curl_getdate` usage and `libcurl.so.4` `DT_NEEDED`). Documented optional GVfs behaviour in `nythera-files/README.md` and added `optdepends` entries (`gvfs`, `gvfs-smb`, `gvfs-mtp`, `gvfs-nfs`) to `nythera-files/packaging/arch/PKGBUILD`.
- **Shared picker identity (D7):** Parameterised `SearchPickerPopupRequest` with `std::string nativeAppId`, removed the hardcoded `"dev.nythera.FileManager.OpenWith"` `appId` from `nythera-lib/src/ui/dialogs/search_picker_popup.cpp` (defaulting to `nullptr` -> `ToplevelSurface`'s `"dev.nythera.Nythera"`), and supplied `.nativeAppId = "dev.nythera.FileManager.OpenWith"` from `nythera-files/src/filemanager/file_menu_controller.cpp`.
- **Shared asset & translation ownership (D8):** Established `nythera-lib/assets/` as the single source of truth for `NytheraDisplay-Regular.ttf`, `nythera-tabler.ttf`, `tabler.json`, `tabler-icons-license.txt`, and the **216** NytheraLib-owned translation keys partitioned losslessly across all **29** locale JSON files. Removed duplicate copies from `nythera-shell` and `nythera-files`, installed shared assets to `<datadir>/nythera-lib/assets/` with `assetsdir` exported in `nythera-lib.pc`, updated `RenderContext`, `GlyphRegistry`, and `i18n::Service` (layered shared + app merging with English fallback), added `nythera-lib/tools/i18n-check.py`, and updated `nythera-shell/tools/i18n-check.py` to auto-resolve shared translations and detect key collisions.
- **Relocatable asset resolution (D3):** Updated `resolveFilesAssetsRoot()` in `nythera-lib/src/core/files/resource_paths.cpp` to resolve executable-relative `../<datadir>/nythera/files-assets` and `../share/nythera/files-assets`, removed stale monorepo `exeDir/assets` and `exeDir.parent_path()/assets` fallbacks, decoupled Files/Shell/NytheraLib root detection predicates, and added `sharedAssetsRoot()` / `sharedAssetPath()` with `dladdr`-based library-relative discovery and `NYTHERA_LIB_ASSETS_DIR` override support.
- **Repository instructions & docs:** Removed obsolete Files/Wastebin/`GVfs` ownership instructions from `nythera-shell/AGENTS.md`, aligned common development/Git/isolation rules across all three `AGENTS.md` files while keeping each self-contained with separate repo-specific sections, and updated `nythera-lib/README.md`, `nythera-files/README.md`, and `nythera-shell/BUILDING.md`.

---

## 2. Implementation findings and decisions

1. **Seven candidate migrated tests actually link Shell-side `.cpp` implementations:**
   Header-only analysis initially suggested 58 library-owned tests, because five headers hosted in `nythera-lib/src/` currently have their `.cpp` implementations in `nythera-shell/src/` (`system/desktop_entry.cpp`, `wayland/ext_foreign_toplevels.cpp`, `wayland/wayland_toplevels.cpp`, `wayland/hyprland/focus_grab_service.cpp`, and part of `ConfigService` mutations in `shell/config/config_mutations.cpp`). Compiling in an isolated `nythera-lib` build caught 7 tests calling those Shell-owned definitions (`app_identity`, `desktop_entry_symlink`, `desktop_entry_watch`, `ext_toplevel_capture_target`, `config_wallpaper_precedence`, `capsule_group_style_persist`, `capsule_group_lane_override`). **Decision:** Returned those 7 tests to `nythera-shell/tests/` and `nythera-shell/meson.build` rather than moving Shell-only `.cpp` implementations into `nythera-lib`.
2. **`url_open_test` mock D-Bus server requires `sdbus-c++`:**
   `url_open_test.cpp` tests `nythera-lib/src/net/url_open.cpp` (genuinely library-owned), but its test fixture creates a mock `org.freedesktop.portal.Desktop` service via `sdbus-c++`. **Decision:** Added `sdbus_cpp_test_dep = dependency('sdbus-c++', required: false)` strictly under `if build_tests` in `nythera-lib/meson.build`, preserving the test unchanged without adding `sdbus-c++` to `nythera_lib` or `nythera-lib.pc`.
3. **Shell directly uses `libcurl` (`curl_getdate`):**
   Although `#include <curl/curl.h>` only appears explicitly in `nythera-lib/src/net/http_client.h`, `nythera-shell/src/scripting/plugin_recommendations.cpp:78` calls `curl_getdate()` directly (transitively via `http_client.h`), and `readelf -d` confirms `[libcurl.so.4]` in `nythera`'s `DT_NEEDED`. **Decision:** Preserved `curl_dep` in `nythera-shell/meson.build` and `curl` in `nythera-shell/packaging/arch/PKGBUILD`.
4. **Files directly links `libtomlplusplus.so.3` via `nythera-lib` headers:**
   `nythera-file-manager` does not include `<toml++/toml.hpp>` directly, nor does `nythera-files` build logic require `tomlplusplus_dep` separately because `nythera-lib.pc` lists `tomlplusplus` in `Requires:`. **Decision:** Removed direct `tomlplusplus_dep` lookup from `nythera-files/meson.build` (provided via `nythera_lib_dep`), while retaining `tomlplusplus` in `nythera-files/packaging/arch/PKGBUILD` `depends` because `nythera-file-manager` carries `[libtomlplusplus.so.3]` in ELF `DT_NEEDED`.
5. **Disjoint root predicates prevent cross-application asset confusion (D3 + D8):**
   - `isSharedAssetRoot`: requires `fonts/NytheraDisplay-Regular.ttf`, `fonts/nythera-tabler.ttf`, `fonts/tabler.json`, and `translations/en.json`.
   - `isAssetRoot` (Shell): requires `emoji.json`, `templates/builtin.toml`, and `translations/en.json` (dropped `fonts/nythera-tabler.ttf` since shared fonts moved to `nythera-lib`).
   - `isFilesAssetRoot` (Files): requires Files-owned `dialogue-window-artwork/top-left-diagonals.svg` and `templates/filemanager/office/blank.odt` (replaces the old font + `en.json` check, preventing Files from matching Shell or NytheraLib directories).
6. **Pre-existing Shell test issues confirmed on `HEAD`:**
   Isolated Shell test builds revealed two pre-existing failures present before Stage 4.3:
   - `plugin_bindings_test.cpp` fails to compile (`luacode.h: No such file or directory`) because `_nythera_test_dependencies` in `nythera-shell/meson.build:794` omitted `luau_dep` prior to Stage 4.3 (verified via `git show 5aee86c0b -- meson.build`).
   - `i18n_supported_languages` fails on `HEAD:assets/translations/en.json` because `settings.widgets.actions.commands` lacks `restart.{label,description}` and `stop.{label,description}` entries corresponding to `kMsgCmd.subcommands`. Both were left unchanged as out-of-scope pre-existing issues and documented for Stage 4.4.

---

## 3. Files changed, grouped by repository

### `/mnt/Vegita/git-repos/nythera-lib` (`dev`)

- **Modified `AGENTS.md`**: Aligned common baseline rules with Shell/Files and documented newly owned unit tests (`tests/`, `just test`), shared assets/translations, and `tools/i18n-check.py`.
- **Modified `README.md`**: Documented `just test`, `share/nythera-lib/assets/` installation contents, shared asset resolution (`paths::sharedAssetPath()`), and `tools/i18n-check.py`.
- **Modified `justfile`**: Added `-Dtests=auto` handling in `configure`/`_ensure-configured`, added `test m=mode *args`, compiled `nythera_lib` in `build`, and expanded `format-code`, `format-code-check`, and `_clang_tidy` to cover `tests` alongside `src`.
- **Modified `meson_options.txt`**: Added `tests` feature option (`value: 'auto'`).
- **Modified `meson.build`**: Added `build_tests`, `NYTHERA_LIB_INSTALL_PREFIX` / `NYTHERA_LIB_INSTALL_DATADIR` compile definitions, `install_subdir('assets', ...)` to `<datadir>/nythera-lib`, `variables: ['assetsdir=${datadir}/nythera-lib/assets']` in `pkg.generate(...)`, `nythera_lib_test_dep`, and 53 Meson test targets.
- **Modified `packaging/arch/PKGBUILD`**: Passed `-Dtests=disabled` to `arch-meson`.
- **Modified `src/core/files/resource_paths.h` & `.cpp`**: Added `sharedAssetsRoot()`, `sharedAssetPath()`, `AssetResolutionContext`, `SharedAssetResolutionContext`, `resolveShellAssetsRoot()`, `resolveFilesAssetsRoot()`, and `resolveSharedAssetsRoot()` (using `dladdr`, `NYTHERA_LIB_ASSETS_DIR`, relative installed/multiarch/dev lookup, and compile-time fallback); updated `isAssetRoot()`, `isFilesAssetRoot()`, and `resolveFilesAssetsRoot()` (executable-relative `../<datadir>/nythera/files-assets` & `../share/nythera/files-assets`; removed stale `exeDir/assets` & `exeDir.parent_path()/assets`).
- **Modified `src/i18n/i18n_service.h` & `.cpp`**: Added layered `shared` + `app` catalogue loading, directory-based `init(...)` overload for isolated testing, collision warning, and stateful `setLanguage(...)`.
- **Modified `src/render/render_context.cpp`**: Updated `nythera-tabler.ttf` and `NytheraDisplay-Regular.ttf` lookups to `paths::sharedAssetPath(...)`.
- **Modified `src/render/text/glyph_registry.cpp`**: Updated `tabler.json` lookup to `paths::sharedAssetPath("fonts/tabler.json")`.
- **Modified `src/ui/dialogs/search_picker_popup.h` & `.cpp`**: Added `std::string nativeAppId` to `SearchPickerPopupRequest` and replaced hardcoded `"dev.nythera.FileManager.OpenWith"` with `!request.nativeAppId.empty() ? request.nativeAppId.c_str() : nullptr`.
- **Created `assets/fonts/`**: Moved `NytheraDisplay-Regular.ttf`, `nythera-tabler.ttf`, `tabler.json`, and `tabler-icons-license.txt` from `nythera-shell`.
- **Created `assets/translations/`**: Created all 29 partitioned locale JSON files (`ar.json` .. `zh-Hant.json`) containing the 216 NytheraLib-owned keys.
- **Created `tests/`**: Copied `test_check.h` and `test_isolation.h`; migrated 51 `*_test.cpp` files from `nythera-shell` (updating `cairo_text_renderer_test.cpp` to `paths::sharedAssetPath(...)` and `file_entry_row_layout_test.cpp` to `NYTHERA_LIB_ASSETS_DIR`); added `resource_paths_test.cpp` and `i18n_catalog_test.cpp`.
- **Created `tools/i18n-check.py`**: Static translation validator for NytheraLib (`--unused`, `--raw`, `--check-locales`).

### `/mnt/Vegita/git-repos/nythera-shell` (`dev`)

- **Modified `AGENTS.md`**: Removed obsolete `FileManager`, `FileOperationManager`, `Wastebin`, `WastebinBackend`, and `.Trash-*` references from `Test Isolation and Data Safety`; aligned common rules baseline; documented Shell-specific vs NytheraLib shared asset ownership.
- **Modified `BUILDING.md`**: Noted NytheraLib's shared `share/nythera-lib/assets/` ownership alongside Shell-specific `<datadir>/nythera/assets/`.
- **Modified `meson.build`**: Removed 14 redundant `nythera-lib`-private dependencies (`wayland-egl`, `egl`/`glesv2`/`epoxy`, `freetype2`, `fontconfig`, `cairo-ft`, `pangoft2`, `harfbuzz`, `librsvg-2.0`, `gio-unix-2.0`, `md4c`, `stb/stb_image_write.h`, `libwebpdemux`, `libjxl`, `libjxl_threads`), removed the 51 migrated test definitions (`_cpp_test_names`, `url_open`, `tooltip_manager`, `native_data_transfer`, `native_data_transfer_wlroots`, `wlroots_test_dep`), and removed `file_entry_row_layout` / `cairo_glyph_renderer` `test_args` branches.
- **Modified `packaging/arch/PKGBUILD`**: Removed `fontconfig`, `freetype2`, `harfbuzz`, `libglvnd`, `libjxl`, `librsvg`, and `md4c` from `depends` and `stb` from `makedepends`.
- **Deleted from `assets/fonts/`**: Removed `NytheraDisplay-Regular.ttf`, `nythera-tabler.ttf`, `tabler.json`, and `tabler-icons-license.txt`.
- **Modified `assets/translations/*.json` (28 files)**: Partitioned out the 216 NytheraLib-owned keys across all locales (`be-Latn.json` remained unchanged because it contained zero NytheraLib keys).
- **Deleted from `tests/`**: Moved 51 `*_test.cpp` files to `nythera-lib/tests/`.
- **Modified `tools/i18n-check.py`**: Added `--shared-translations` (auto-resolving via `NYTHERA_LIB_ASSETS_DIR`, `pkg-config --variable=assetsdir nythera-lib`, or sibling checkout fallback), collision detection between shared and Shell catalogues, and combined `shared ∪ shell` missing-key verification.
- **Created `audits-investigations/stage-4.3-architectural-cleanup.md`**: This report.

### `/mnt/Vegita/git-repos/nythera-files` (`dev`)

- **Modified `AGENTS.md`**: Aligned common rules baseline with NytheraLib and Nythera Shell while preserving Files-specific Polkit, Wastebin, GVfs, and standalone invariants.
- **Modified `README.md`**: Documented optional GVfs functionality (`trash://`, volume/remote mounting, `gvfs-smb`/`gvfs-mtp`/`gvfs-nfs` backends, and graceful fallback) and NytheraLib's ownership of shared fonts and library translations.
- **Modified `meson.build`**: Removed 19 redundant `nythera-lib`-private dependencies (`wayland-egl`, `egl`/`glesv2`/`epoxy`, `freetype2`, `fontconfig`, `cairo`, `cairo-ft`, `pango`, `pangocairo`, `pangoft2`, `harfbuzz`, `librsvg-2.0`, `libcurl`, `md4c`, `nlohmann_json`, `tomlplusplus`, `libwebp`, `libwebpdemux`, `libjxl`, `libjxl_threads`, `dl`) and removed unused `jemalloc_cpp_args` (`-DNYTHERA_USE_JEMALLOC=1`) while keeping `jemalloc_deps`.
- **Modified `packaging/arch/PKGBUILD`**: Pruned 12 private dependencies from `depends` (`cairo`, `curl`, `fontconfig`, `freetype2`, `harfbuzz`, `libglvnd`, `libjxl`, `librsvg`, `libwebp`, `md4c`, `nlohmann-json`, `pango`) and added `optdepends` for `gvfs`, `gvfs-smb`, `gvfs-mtp`, and `gvfs-nfs`.
- **Modified `src/filemanager/file_menu_controller.cpp`**: Passed `.nativeAppId = "dev.nythera.FileManager.OpenWith"` in `openApplicationChooser()`.
- **Modified `files-assets/meson.build`**: Removed `subdir('fonts')` and `subdir('translations')`.
- **Deleted `files-assets/fonts/*` (4 font/metadata files) & `files-assets/translations/*.json` (29 JSON files)**: Removed redundant shared copies (see Section 12 regarding the two unreferenced `meson.build` files blocked by sandbox bind-mounts).

---

## 4. Migrated and retained test ownership map

### Migrated from `nythera-shell` to `nythera-lib` (**51** tests)

| Category | Migrated Meson tests | Reason |
| --- | --- | --- |
| Core / Config / CLI | `cli_help`, `cli_parse`, `cli_schema`, `config_migration`, `config_path_resolution`, `config_widget`, `inotify`, `log`, `process`, `state_store`, `string_utils`, `url_open` | Exclusively exercise `nythera-lib` `src/core/`, `src/config/`, `src/cli/`, `src/net/` (test-only `sdbus-c++` mock kept for `url_open`). |
| i18n / Time / Theme | `fixed_palette_json`, `i18n_language_tag`, `notification_filter`, `scheme`, `theme_shell_mode`, `time_format` | Exclusively exercise `nythera-lib` `src/i18n/`, `src/time/`, `src/theme/`, `src/notification/`. |
| Render / Images / Text | `animation_manager`, `cairo_glyph_renderer`, `cairo_text_renderer`, `ico_decoder`, `image_file_loader_data_uri`, `image_source_log`, `jxl_decoder`, `surface_blur_region`, `surface_haze_render`, `thumbnail_service`, `webp_decoder` | Exclusively exercise `nythera-lib` `src/render/`. |
| System / Wayland | `freedesktop_key_file`, `icon_resolver`, `monitor_selector`, `native_data_transfer`, `native_data_transfer_wlroots`, `wayland_output_scale`, `wayland_seat_keyboard` | Exclusively exercise `nythera-lib` `src/system/` and `src/wayland/` (including wlroots/wayland-server test harnesses). |
| Shared UI & Dialogs | `button_layout`, `calendar_view_passed_event`, `capsule_group_reconcile`, `file_entry_row_layout`, `grid_item_cursor`, `input_area`, `input_bidi`, `input_dispatcher`, `input_password_mode`, `input_read_only`, `input_readline_shortcuts`, `scrollbar_geometry`, `search_picker_selection`, `tooltip_manager`, `virtual_grid_view_gesture` | Exclusively exercise `nythera-lib` `src/ui/` controls, `FileEntryRow`/`FileEntryTile`, and `TooltipManager`. |

### Added in `nythera-lib` (**2** regression tests)

- `resource_paths`: Verifies env overrides, dev/installed/relocated lookup, multiarch `lib/<arch>/` discovery, compile-time fallback, rejection of stale monorepo fallbacks, and cross-app isolation for Files, Shell, and NytheraLib.
- `i18n_catalog`: Validates NytheraLib's 29 locale files against `kSupportedLanguages` plus layered shared+app loading, precedence, English fallback, interpolation, and pluralisation.

### Ambiguous or retained in `nythera-shell` (**151** test source files / **159** Meson test cases)

| Test(s) | Ownership decision | Reasoning |
| --- | --- | --- |
| `app_identity`, `desktop_entry_symlink`, `desktop_entry_watch` | Retained in **Shell** | Although `system/desktop_entry.h` resides in `nythera-lib`, `scanDesktopEntries()`, `desktopEntries()`, `desktopEntryWatchFd()`, and `checkDesktopEntryReload()` are implemented in `nythera-shell/src/system/desktop_entry.cpp`. |
| `ext_toplevel_capture_target` | Retained in **Shell** | Tests `uniqueExtHandleForTitle()`, whose `.h` is in `nythera-lib` (`ext_foreign_toplevels.h`) while its `.cpp` implementation lives in `nythera-shell/src/wayland/ext_foreign_toplevels.cpp`. |
| `config_wallpaper_precedence`, `capsule_group_style_persist`, `capsule_group_lane_override` | Retained in **Shell** | Exercise `ConfigService` mutator methods (`setOverride`, `setOverrides`, `setWallpaperPath`, `applyWallpaperSelection`, `resetBarLaneOverride`) implemented in `nythera-shell/src/shell/config/config_mutations.cpp`. |
| `i18n_supported_languages` | Retained in **Shell** | Validates Shell's `assets/translations/` completeness AND checks Shell-owned `settings.widgets.actions.commands` keys against `kMsgCmd.subcommands`. (Paired with new `i18n_catalog` in `nythera-lib`.) |
| `uri`, `format_units`, `kde_color_scheme`, `firefox_theme_manifest`, `font_family_catalog`, `keyboard_layout_label`, `legacy_profile_migration`, `surface_haze`, `audio_*`, `battery_*`, `caldav_*`, `calendar_*`, `clipboard_*`, `config_override_mutation`, `config_schema_roundtrip`, `context_menu_popup`, `cpu_*`, `desktop_entry_localization`, `desktop_entry_launch`, `desktop_widget_output`, `disk_mounts`, `dock_*`, `dynamic_island`, `enterprise_credentials`, `event_*`, `fcitx_status`, `google_client_*`, `greeter_appearance_sync`, `gtk_extensions`, `hook_manager`, `hyprland_*`, `ical_parser`, `image_orientation`, `ipc_service`, `iwd_*`, `job_progress*`, `labwc_*`, `lane_selection_token`, `launcher_*`, `location_service`, `lockscreen_*`, `math_provider`, `network_*`, `niri_*`, `notification_*` (excluding `notification_filter`), `panel_resize`, `path_browse`, `pipewire_error`, `pixel_stream_client`, `plugin_*`, `privacy_*`, `screen_wrap_geometry`, `screenshot_*`, `secret_store`, `security_primitives`, `sway_*`, `system_monitor_service`, `taskbar_widget`, `template_*`, `toplevel_*`, `tray_*` (9 integration cases), `umbriel_audio_provider`, `upower_*`, `upstream_plugin_source`, `v4l2_camera`, `vdir_reader`, `wallpaper_*`, `wayland_toplevels_identity`, `widget_*`, `window_*`, `workspace_*`, plus 5 script tests (`config_validate_cli`, `discord_template`, `template_undo_signal`, `umbriel_apply_include_comment`, `compositor_template_config`) | Retained in **Shell** | Directly include and test `nythera-shell/src/` implementations (`net/uri.cpp` in Shell, Shell services, compositors, panels, templates, or D-Bus/Wayland integrations). Note: `uri_test.cpp` tests Shell's `notification_history_store.h` and `clipboard_service.h` URI behaviour alongside `net/uri.h`. |

*(Total `nythera-lib` tests: **53**; `nythera-shell` tests: **159** Meson test targets across 151 source/script files; `nythera-files` tests: **34** unchanged.)*

---

## 5. Removed dependencies and supporting evidence

### Nythera Files (`nythera-files/meson.build` & `packaging/arch/PKGBUILD`)

- **Removed from `nythera-files/meson.build` (19 dependencies + 1 unused macro):**
  `wayland-egl`, `egl`, `glesv2`, `epoxy`, `freetype2`, `fontconfig`, `cairo`, `cairo-ft`, `pango`, `pangocairo`, `pangoft2`, `harfbuzz`, `librsvg-2.0`, `libcurl`, `md4c`, `nlohmann_json`, `tomlplusplus`, `libwebp`, `libwebpdemux`, `libjxl`, `libjxl_threads`, `dl`, plus `-DNYTHERA_USE_JEMALLOC=1` (`jemalloc_cpp_args`).
  - **Evidence:** `rg` across `nythera-files/src` and `nythera-files/tests` found zero direct `#include`s or symbol references for any of those 19 dependencies or `NYTHERA_USE_JEMALLOC` (allocator replacement continues unchanged via linked `jemalloc_deps`). `nythera-lib.pc` `Requires:` still supplies compile/include flags for transitive header dependencies (`toml++`, `nlohmann/json_fwd.hpp`, `EGL/egl.h`, `GLES2/gl2.h`, `curl/curl.h`).
- **Removed from `nythera-files/packaging/arch/PKGBUILD` (`depends`, 12 packages):**
  `cairo`, `curl`, `fontconfig`, `freetype2`, `harfbuzz`, `libglvnd`, `libjxl`, `librsvg`, `libwebp`, `md4c`, `nlohmann-json`, `pango`.
  - **Preserved in `depends`:** `'nythera-lib>=0.1.0'`, `'nythera-lib<0.2.0'`, `libnythera_lib.so`, `glib2`, `jemalloc`, `libgcc`, `libstdc++`, `libxkbcommon`, `polkit`, `tomlplusplus` (`libtomlplusplus.so.3` is in `nythera-file-manager` ELF `DT_NEEDED`), `wayland`.
- **Added to `nythera-files/packaging/arch/PKGBUILD` (`optdepends`):**
  `gvfs`, `gvfs-smb`, `gvfs-mtp`, `gvfs-nfs`.

### Nythera Shell (`nythera-shell/meson.build` & `packaging/arch/PKGBUILD`)

- **Removed from `nythera-shell/meson.build` (14 dependencies/checks):**
  `wayland-egl`, `egl`, `glesv2`, `epoxy`, `freetype2`, `fontconfig`, `cairo-ft`, `pangoft2`, `harfbuzz`, `librsvg-2.0`, `gio-unix-2.0`, `md4c`, `stb/stb_image_write.h` header check, `libwebpdemux`, `libjxl`, `libjxl_threads`.
  - **Evidence:** `rg` across `nythera-shell/src` and remaining `nythera-shell/tests` confirmed zero direct `#include`s or symbol usage after migrating `cairo_text_renderer_test` (`fontconfig`/`pangofc-fontmap`) and `surface_haze_render_test` (`EGL/egl.h`) to `nythera-lib`. `readelf -d` on the staged `nythera` binary confirms `[libnythera_lib.so.0]` plus only directly linked dependencies (`wayland-client`, `pango`, `cairo`, `xkbcommon`, `gio`, `gobject`, `glib`, `curl`, `tomlplusplus`, `pangocairo`, `webp`, `jemalloc`, `secret`, `sodium`, `sndfile`, `xml2`, `sdbus-c++`, `polkit-agent-1`, `polkit-gobject-1`, `pipewire-0.3`, `wireplumber-0.5`, `qalculate`, `pam`, `ical`).
  - **Preserved `curl_dep` / `curl`:** `nythera-shell/src/scripting/plugin_recommendations.cpp:78` directly calls `curl_getdate()`, and `libcurl.so.4` appears in `nythera` ELF `DT_NEEDED`.
- **Removed from `nythera-shell/packaging/arch/PKGBUILD`:**
  `depends`: `fontconfig`, `freetype2`, `harfbuzz`, `libglvnd`, `libjxl`, `librsvg`, `md4c`.  
  `makedepends`: `stb`.

---

## 6. Shared picker identity changes

- **`nythera-lib/src/ui/dialogs/search_picker_popup.h`:** Added `std::string nativeAppId;` to `SearchPickerPopupRequest`.
- **`nythera-lib/src/ui/dialogs/search_picker_popup.cpp:221`:** Changed `.appId = "dev.nythera.FileManager.OpenWith"` to `.appId = !request.nativeAppId.empty() ? request.nativeAppId.c_str() : nullptr` (allowing `ToplevelSurface::initialize` to use its neutral `"dev.nythera.Nythera"` fallback when unset).
- **`nythera-files/src/filemanager/file_menu_controller.cpp:1412`:** `openApplicationChooser()` sets `.nativeAppId = "dev.nythera.FileManager.OpenWith"`, preserving Files' Open With window identity.
- **`nythera-shell`:** `SettingsWindow::openSearchPickerPopup` (`settings_window_popups.cpp:543`) and `WallpaperPanel` (`wallpaper_panel.cpp:1370`) use `xdg_popup` (`nativeWindow = false`) and no longer inherit any Files application ID.

---

## 7. Shared and application-specific asset ownership map

| Owner | Path / install location | Owned resources |
| --- | --- | --- |
| **NytheraLib** | `nythera-lib/assets/` -> `<datadir>/nythera-lib/assets/` | `fonts/NytheraDisplay-Regular.ttf`, `fonts/nythera-tabler.ttf`, `fonts/tabler.json`, `fonts/tabler-icons-license.txt`, and `translations/*.json` (29 locales, 216 shared keys in `en.json`). |
| **Nythera Shell** | `nythera-shell/assets/` -> `<datadir>/nythera/assets/` | Shell icons/wallpapers/branding artwork, `emoji.json`, `firefox_theme/`, `palettes/`, `templates/`, `.desktop` files, `nythera.svg`, and Shell-specific `translations/*.json` (29 locales, 3,316 Shell keys in `en.json`). |
| **Nythera Files** | `nythera-files/files-assets/` -> `<datadir>/nythera/files-assets/` | `LICENSE`, `concepts/nythera-files-icon.png`, `dialogue-window-artwork/` (`top-left-diagonals.svg`, `bottom-right-contour.svg`), and `templates/filemanager/office/` (`blank.odt`, `blank.ods`, `blank.odp`). |

*(Note: `nythera-shell/tools/generate_tabler_metadata.py` and `nythera-files/files-assets/LICENSE` line 65 referencing `fonts/tabler-icons-license.txt` were left untouched per Stage 4.3 scope constraints.)*

---

## 8. Translation catalogue ownership, loading and fallback behaviour

- **Key partitioning:** All **216** keys referenced in `nythera-lib/src/` via `i18n::tr`, `i18n::trp` (+ `-plural`), dotted key literals, and dynamic prefixes (including all existing `-plural` pairs in `en.json`) were extracted losslessly across all **29** locale files into `nythera-lib/assets/translations/*.json`. The remaining **3,316** keys stayed in `nythera-shell/assets/translations/*.json` with empty parent objects pruned and zero string modifications or machine translations. `nythera-files/files-assets/translations/` was removed (`nythera-files/src/` currently makes 0 direct `i18n::tr` calls and consumes NytheraLib strings via shared dialogs and `time_format`).
- **Runtime layered loading (`i18n::Service`):**
  1. `init(preferredLang)` delegates to `init(preferredLang, paths::sharedAssetPath("translations"), paths::assetPath("translations"))`.
  2. For any language `L`, `loadCatalog` loads `sharedDir/<L>.json` first, then layers `appDir/<L>.json` (when `appDir != sharedDir` and present). Application keys override shared keys deterministically (logging a warning if values differ).
  3. English (`"en"`) is loaded first into `m_fallback` (shared + app), then the requested/environment locale via `catalogLanguageCandidates()` into `m_active`.
  4. `lookup()` checks `m_active`, then `m_fallback` (preserving per-key fallback across both layers as well as locale-level fallback, `detail::interpolate`, `trp` `-plural` selection, and RTL detection).
- **Validation tooling:** `nythera-lib/tools/i18n-check.py` checks `nythera-lib/src/` against `nythera-lib/assets/translations/en.json` and validates all 29 locale files (`--check-locales`). `nythera-shell/tools/i18n-check.py` resolves shared translations via `--shared-translations`, `NYTHERA_LIB_ASSETS_DIR`, `pkg-config --variable=assetsdir nythera-lib`, or sibling checkout fallback, failing on any key collision or missing key across `shared ∪ shell`.

---

## 9. Resource resolution behaviour before and after

| Function | Before Stage 4.3 | After Stage 4.3 |
| --- | --- | --- |
| `resolveFilesAssetsRoot` | Checked `NYTHERA_ASSETS_DIR`, `exeDir/files-assets`, stale `exeDir/assets` & `exeDir.parent_path()/assets`, then jumped straight to compile-time `installedFilesAssetsRoot()`. Required `fonts/NytheraDisplay-Regular.ttf` + `translations/en.json`. | Checks `NYTHERA_ASSETS_DIR`, `exeDir/files-assets`, executable-relative `exeDir.parent_path()/<datadir>/nythera/files-assets` (if relative `datadir`), `exeDir.parent_path()/share/nythera/files-assets`, then compile-time `installedFilesAssetsRoot()`. Drops stale `assets` paths and validates Files-owned artwork/templates (`dialogue-window-artwork/top-left-diagonals.svg` + `templates/filemanager/office/blank.odt`). |
| `resolveShellAssetsRoot` | Checked `NYTHERA_ASSETS_DIR`, `exeDir/assets`, `exeDir.parent_path()/assets`, exe-relative `<datadir>/nythera/assets` & `share/nythera/assets`, compile-time fallback. Required `fonts/nythera-tabler.ttf`. | Same lookup order; `isAssetRoot()` checks Shell-owned `emoji.json`, `templates/builtin.toml`, and `translations/en.json` without requiring `fonts/nythera-tabler.ttf`. |
| `resolveSharedAssetsRoot` | Did not exist (fonts and glyph metadata resolved via `paths::assetPath(...)`). | Checks `NYTHERA_LIB_ASSETS_DIR`, then `dladdr`-derived library-relative paths (`<libDir>/../<datadir>/nythera-lib/assets`, `<libDir>/../../<datadir>/nythera-lib/assets`, `<libDir>/../share/nythera-lib/assets`, `<libDir>/../../share/nythera-lib/assets`, `<libDir>/assets`, `<libDir>/../assets`), then compile-time `NYTHERA_LIB_INSTALL_PREFIX` / `NYTHERA_LIB_INSTALL_DATADIR`. |

---

## 10. Validation results

All builds and tests ran in isolated temporary directories (`/tmp/nythera-stage43-*`, cleaned up afterward). Active `build-debug/` trees (`intro-buildoptions.json` timestamps `02:52–02:59` UTC) and `$HOME/.local/opt/nythera-lib` were never modified.

| Check | Result | Scope & Notes |
| --- | --- | --- |
| `nythera-lib` isolated build, test, and staged install | **Passed (`53/53`, 0 failed, 0 skipped)** | Re-run after final formatting edit in `resource_paths.cpp`. All 51 migrated tests + `resource_paths` + `i18n_catalog` passed (`surface_haze_render`, `url_open`, `native_data_transfer`, `native_data_transfer_wlroots`, `wayland_seat_keyboard`, `cairo_glyph_renderer`, `cairo_text_renderer`, `file_entry_row_layout` included). Staged install confirmed 33 shared assets in `share/nythera-lib/assets/` and clean `pkg-config --variable=assetsdir nythera-lib`. |
| `nythera-files` isolated build, test, and staged install | **Compiled cleanly; `32/34` tests passed (`2` sandbox-blocked); staged install clean** | Built against staged `nythera-lib` with 19 redundant deps removed. `32/34` tests passed (including `admin_mode_stage3`, `file_manager_help_menu`, `file_artwork`, `file_wastebin_operation`, `places_model`, `mount_backends`, `test_isolation`). Two pre-existing tests hit sandbox limits: `file_transfer_pause` (`mkdtemp` in `/var/tmp`, read-only in sandbox) and `file_operation_manager` (`lchown` to supplementary GID unmapped in user-ns sandbox). Staged `--tags files,files-assets` installed 0 font/translation files. `readelf -d` confirmed pruned `DT_NEEDED` (`libnythera_lib.so.0`, `libwayland-client`, `libgio`, `libgobject`, `libglib`, `libtomlplusplus`, `libpolkit-gobject`, `libjemalloc`). |
| `nythera-shell` isolated build, focused tests, and staged install | **Shell binary & completions compiled; `11/12` focused tests passed; staged install clean** | Built against staged `nythera-lib` with 14 redundant deps removed. `meson test --list` confirmed **159** targets (all 51 migrated tests absent, 7 restored tests present). `11/12` focused tests passed (`desktop_entry_localization`, `annotation_raster`, `font_family_catalog`, `config_schema_roundtrip`, `app_identity`, `ext_toplevel_capture_target`, `config_wallpaper_precedence`, `desktop_entry_symlink`, `desktop_entry_watch`, `capsule_group_style_persist`, `capsule_group_lane_override`). Staged install confirmed `share/nythera/assets/fonts/` contains no shared font/metadata files. Two pre-existing `HEAD` issues confirmed: `i18n_supported_languages` (`restart`/`stop` missing in `en.json`) and `plugin_bindings_test.cpp` (`_nythera_test_dependencies` missing `luau_dep`). |
| `nythera-lib/tools/i18n-check.py --unused --fail-on-unused --check-locales` | **Passed** | `0` missing keys, `0` unused keys (`216` entries), all 29 locales valid and consistent with `en.json`. |
| `nythera-shell/tools/i18n-check.py` | **Passed** | `1050` `tr` + `5` `trp` base keys all resolved across `3316` app + `216` shared entries; `0` collisions. |
| `just --list` (`nythera-lib`) | **Passed** | Confirmed `test` recipe and `format`/`lint` updates present. |
| `clang-format --dry-run -Werror` (touched C++ files/lines) | **Passed** | Exited 0 on all new/modified C++ files in `nythera-lib` and modified lines in `file_menu_controller.cpp` & migrated tests. |
| `git diff --check` across all 3 repos | **Passed** | Exited 0 with no whitespace errors in any repository. |

---

## 11. Outstanding checks for Stages 4.4 and 4.5

- **Stage 4.4 (Coordinated active-tree rebuild & full runtime regression):**
  - Reconfigure/rebuild `nythera-lib/build-debug`, install into `$HOME/.local/opt/nythera-lib`, and rebuild `nythera-shell/build-debug` and `nythera-files/build-debug` against the updated headers (`resource_paths.h`, `i18n_service.h`, `search_picker_popup.h`).
  - Run the full unsandboxed test suites, including `nythera-files:file_transfer_pause` (`/var/tmp`), `nythera-files:file_operation_manager` (`lchown` supplementary group), and the remaining `nythera-shell` tests (`plugin_bindings` once `_nythera_test_dependencies` is reconciled, plus `i18n_supported_languages` once `restart`/`stop` action translations are addressed).
  - Reconcile host Polkit helpers/policies (`D1`, `D4`) and verify live Umbriel `app_id` and Open With picker behaviour.
- **Stage 4.5 (Clean-environment dependency & packaging validation):**
  - Build and install all three repositories from scratch in a clean container/VM environment using the pruned `meson.build` and Arch `PKGBUILD` dependency lists (verifying `gvfs` optional runtime fallback when GVfs packages are absent vs present, and confirming zero hidden build/runtime dependencies).

---

## 12. Deviations, blockers or unresolved decisions

1. **Two unreferenced empty-directory `meson.build` files in `nythera-files`:** Inside the execution sandbox, `files-assets/fonts/meson.build` and `files-assets/translations/meson.build` are individual file bind-mounts in the container namespace, so `unlink()` failed with `EBUSY`. Both subdirectories were removed from `nythera-files/files-assets/meson.build` (so Meson never reads or installs them), and all 4 font files and 29 JSON files inside them were deleted. Before committing on the host, remove those two leftover files via `rm -rf /mnt/Vegita/git-repos/nythera-files/files-assets/fonts /mnt/Vegita/git-repos/nythera-files/files-assets/translations`.
2. **Pre-existing Shell test issues left untouched:** `plugin_bindings_test` (`luau_dep` omitted from `_nythera_test_dependencies` in `nythera-shell/meson.build:794` on `HEAD`) and `i18n_supported_languages` (`settings.widgets.actions.commands.{restart,stop}` missing in `HEAD:assets/translations/en.json`) were left unchanged as outside Stage 4.3 scope.
3. **Out-of-scope residual cross-references:** `nythera-files/files-assets/LICENSE` (line 65 referencing `fonts/tabler-icons-license.txt`) and `nythera-shell/tools/generate_tabler_metadata.py` were intentionally left untouched.

---

## 13. Git working-tree status across all three repositories

### `/mnt/Vegita/git-repos/nythera-lib` (`dev`)

```text
 M AGENTS.md
 M README.md
 M justfile
 M meson.build
 M meson_options.txt
 M packaging/arch/PKGBUILD
 M src/core/files/resource_paths.cpp
 M src/core/files/resource_paths.h
 M src/i18n/i18n_service.cpp
 M src/i18n/i18n_service.h
 M src/render/render_context.cpp
 M src/render/text/glyph_registry.cpp
 M src/ui/dialogs/search_picker_popup.cpp
 M src/ui/dialogs/search_picker_popup.h
?? assets/
?? tests/
?? tools/
```

### `/mnt/Vegita/git-repos/nythera-shell` (`dev`)

```text
 M AGENTS.md
 M BUILDING.md
 D assets/fonts/NytheraDisplay-Regular.ttf
 D assets/fonts/nythera-tabler.ttf
 D assets/fonts/tabler-icons-license.txt
 D assets/fonts/tabler.json
 M assets/translations/*.json (28 modified locale files; be-Latn.json unchanged)
 M meson.build
 M packaging/arch/PKGBUILD
 D tests/<51 migrated *_test.cpp files>
 M tools/i18n-check.py
?? audits-investigations/NYTHERA-REPOSITORY-SPLIT-STAGES-1-AND-2-SUMMARY.md
?? audits-investigations/NYTHERA-THREE-WAY-REPOSITORY-SPLIT-AUDIT.md
?? audits-investigations/post-separation-development-readiness.md
?? audits-investigations/stage-4.3-architectural-cleanup.md
```

### `/mnt/Vegita/git-repos/nythera-files` (`dev`)

```text
 M AGENTS.md
 M README.md
 D files-assets/fonts/NytheraDisplay-Regular.ttf
 D files-assets/fonts/nythera-tabler.ttf
 D files-assets/fonts/tabler-icons-license.txt
 D files-assets/fonts/tabler.json
 M files-assets/meson.build
 D files-assets/translations/*.json (29 deleted locale files)
 M meson.build
 M packaging/arch/PKGBUILD
 M src/filemanager/file_menu_controller.cpp
```
