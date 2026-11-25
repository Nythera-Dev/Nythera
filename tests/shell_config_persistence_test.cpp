#include "config/config_credentials.h"
#include "config/config_service.h"
#include "config/config_types.h"
#include "config/config_validate.h"
#include "core/timer_manager.h"
#include "core/toml.h"
#include "ipc/ipc_service.h"
#include "shell/config/config_validation_hooks.h"
#include "shell/settings/settings_reload_gate.h"
#include "test_check.h"
#include "test_isolation.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <poll.h>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

  namespace fs = std::filesystem;

  void writeFile(const fs::path& path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    TEST_CHECK(out << content);
  }

  std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    TEST_CHECK(in);
    return {std::istreambuf_iterator<char>(in), {}};
  }

  const BarConfig* findBar(const Config& cfg, std::string_view name) {
    const auto it = std::ranges::find(cfg.bars, name, &BarConfig::name);
    return it != cfg.bars.end() ? &*it : nullptr;
  }

  const BarMonitorOverride* findBarMonitor(const BarConfig& bar, std::string_view match) {
    const auto it = std::ranges::find(bar.monitorOverrides, match, &BarMonitorOverride::match);
    return it != bar.monitorOverrides.end() ? &*it : nullptr;
  }

  void testSparseDefaultsAndFirstWrite() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-sparse"});
    const fs::path shellDir = env.config() / "nythera" / "shell";
    const fs::path configPath = shellDir / "config.toml";
    const fs::path legacySettings = env.state() / "nythera" / "settings.toml";

    TEST_CHECK(!fs::exists(shellDir));
    ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
    TEST_CHECK(config.activeConfigDir() == shellDir);
    TEST_CHECK(!fs::exists(shellDir));
    TEST_CHECK(!fs::exists(legacySettings));
    TEST_CHECK(config.config().accessibility.uiScale == 1.0F);
    TEST_CHECK(!config.hasOverride({"accessibility", "ui_scale"}));

    // Setting a value equal to the built-in default on a sparse config keeps it sparse.
    bool changed = false;
    TEST_CHECK(config.setOverride({"accessibility", "ui_scale"}, 1.0, &changed));
    TEST_CHECK(!changed);
    TEST_CHECK(!fs::exists(configPath));

    // Writing a non-default value creates ~/.config/nythera/shell/config.toml directly.
    TEST_CHECK(config.setOverride({"accessibility", "ui_scale"}, 1.25, &changed));
    TEST_CHECK(changed);
    TEST_CHECK(fs::exists(configPath));
    TEST_CHECK(!fs::exists(legacySettings));
    TEST_CHECK(config.config().accessibility.uiScale == 1.25F);
    TEST_CHECK(config.hasOverride({"accessibility", "ui_scale"}));
    TEST_CHECK(config.hasEffectiveOverride({"accessibility", "ui_scale"}));

    ConfigService reopened{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
    TEST_CHECK(reopened.config().accessibility.uiScale == 1.25F);
    TEST_CHECK(toml::parse_file(configPath.string())["accessibility"]["ui_scale"].value<double>() == 1.25);
  }

  void testIncludesPrecedenceAndSessionActionMerging() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-includes"});
    const fs::path shellDir = env.config() / "nythera" / "shell";
    const fs::path base = shellDir / "00-base.toml";
    const fs::path host = shellDir / "90-host.toml";
    const fs::path includeDir = env.work() / "inc-dir";
    const fs::path incDirFile = includeDir / "10-shared.toml";
    const fs::path externalTarget = env.work() / "external-theme.toml";
    const fs::path symlinkInclude = env.work() / "symlink-theme.toml";

    writeFile(base, R"(
# Base file comment
[bar.default]
thickness = 32

[shell.session]
actions = [
  { action = "lock", label = "Base Lock", enabled = true },
  { action = "suspend", label = "Base Suspend", enabled = true },
]
)");

    writeFile(incDirFile, R"(
# Shared directory include
[bar.default]
thickness = 38
[shell.session]
actions = [
  { action = "lock", command = "loginctl lock-session" },
]
)");

    writeFile(externalTarget, R"(
# External symlinked include
[theme]
# User mode preference
mode = "light"
[bar.default.monitor."DP.1"]
thickness = 44
)");
    fs::create_symlink(externalTarget, symlinkInclude);

    writeFile(host, R"(
[include]
files = ["../../../work/inc-dir", "../../../work/symlink-theme.toml"]

[accessibility]
ui_scale = 1.15
)");

    ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
    TEST_CHECK(config.config().bars.at(0).thickness == 38);
    TEST_CHECK(config.config().theme.mode == ThemeMode::Light);
    TEST_CHECK(config.config().accessibility.uiScale == 1.15F);
    const auto* mon = findBarMonitor(config.config().bars.at(0), "DP.1");
    TEST_CHECK(mon != nullptr && mon->thickness == 44);

    // Special [shell.session].actions per-index merging preserves base fields when action matches.
    const auto& actions = config.config().shell.session.actions;
    TEST_CHECK(actions.size() == 1);
    TEST_CHECK(actions[0].action == "lock");
    TEST_CHECK(actions[0].label == "Base Lock");
    TEST_CHECK(actions[0].command == "loginctl lock-session");

    // Editing an existing setting updates its winning definition (through the symlink to externalTarget)
    // and preserves comments + symlink integrity.
    TEST_CHECK(config.setOverride({"theme", "mode"}, std::string("dark")));
    TEST_CHECK(fs::is_symlink(symlinkInclude));
    const std::string extContent = readFile(externalTarget);
    TEST_CHECK(extContent.contains("# External symlinked include"));
    TEST_CHECK(extContent.contains("# User mode preference"));
    TEST_CHECK(toml::parse_file(externalTarget.string())["theme"]["mode"].value<std::string>() == "dark");
    TEST_CHECK(!toml::parse_file(host.string()).contains("theme"));

    // Writing a new setting inside an existing table routes to that owning table's file.
    TEST_CHECK(config.setOverride({"bar", "default", "monitor", "DP.1", "padding"}, std::int64_t{22}));
    TEST_CHECK(
        toml::parse_file(externalTarget.string())["bar"]["default"]["monitor"]["DP.1"]["padding"].value<int>() == 22
    );
    TEST_CHECK(!toml::parse_file(host.string()).contains("bar"));

    // Writing a new top-level section routes to ~/.config/nythera/shell/config.toml.
    TEST_CHECK(config.setOverride({"dock", "enabled"}, true));
    const fs::path defaultFile = shellDir / "config.toml";
    TEST_CHECK(fs::exists(defaultFile));
    TEST_CHECK(toml::parse_file(defaultFile.string())["dock"]["enabled"].value<bool>() == true);

    // Autoload opt-out ignores 00-base.toml and config.toml when 90-host.toml sets autoload = false.
    writeFile(host, R"(
[include]
autoload = false
files = ["../../../work/symlink-theme.toml"]

[accessibility]
ui_scale = 1.35
)");
    config.forceReload();
    TEST_CHECK(config.config().accessibility.uiScale == 1.35F);
    TEST_CHECK(config.config().bars.at(0).thickness == BarConfig{}.thickness);
    TEST_CHECK(!config.config().dock.enabled);
  }

  void testResetAndEntityDeletionAcrossFiles() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-reset-delete"});
    const fs::path shellDir = env.config() / "nythera" / "shell";
    const fs::path f1 = shellDir / "00-one.toml";
    const fs::path f2 = shellDir / "10-two.toml";

    writeFile(f1, R"(
# File one
[bar.default]
thickness = 36

[bar.extra]
enabled = true

[bar.extra.monitor."HDMI-1"]
thickness = 48

[dock.monitor."DP-2"]
icon_size = 40

[calendar.account.work]
type = "ics"
server_url = "https://example.invalid/work.ics"
)");
    writeFile(f2, R"(
# File two
[bar.default]
thickness = 42

[bar.extra]
position = "bottom"

[bar.extra.monitor."HDMI-1"]
padding = 12

[dock.monitor."DP-2"]
enabled = true

[calendar.account.work]
enabled = true
)");

    ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
    TEST_CHECK(config.config().bars.at(0).thickness == 42);
    TEST_CHECK(findBar(config.config(), "extra") != nullptr);

    // Resetting bar.default.thickness removes BOTH contributing definitions and restores built-in default.
    TEST_CHECK(config.clearOverride({"bar", "default", "thickness"}));
    TEST_CHECK(config.config().bars.at(0).thickness == BarConfig{}.thickness);
    TEST_CHECK(!toml::parse_file(f1.string())["bar"]["default"]["thickness"]);
    TEST_CHECK(!toml::parse_file(f2.string())["bar"]["default"]["thickness"]);
    TEST_CHECK(readFile(f1).contains("# File one") && readFile(f2).contains("# File two"));

    // Deleting entities removes them across all contributing files.
    TEST_CHECK(config.deleteMonitorOverride("extra", "HDMI-1"));
    TEST_CHECK(!toml::parse_file(f1.string())["bar"]["extra"]["monitor"]["HDMI-1"]);
    TEST_CHECK(!toml::parse_file(f2.string())["bar"]["extra"]["monitor"]["HDMI-1"]);
    TEST_CHECK(findBarMonitor(*findBar(config.config(), "extra"), "HDMI-1") == nullptr);

    TEST_CHECK(config.deleteBarOverride("extra"));
    TEST_CHECK(findBar(config.config(), "extra") == nullptr);
    TEST_CHECK(!toml::parse_file(f1.string())["bar"]["extra"]);
    TEST_CHECK(!toml::parse_file(f2.string())["bar"]["extra"]);

    TEST_CHECK(config.deleteDockMonitorOverride("DP-2"));
    TEST_CHECK(config.config().dock.monitorOverrides.empty());
    TEST_CHECK(!toml::parse_file(f1.string())["dock"]["monitor"]["DP-2"]);
    TEST_CHECK(!toml::parse_file(f2.string())["dock"]["monitor"]["DP-2"]);

    TEST_CHECK(config.deleteCalendarAccountOverride("work"));
    TEST_CHECK(config.config().calendar.accounts.empty());
    TEST_CHECK(!toml::parse_file(f1.string())["calendar"]["account"]["work"]);
    TEST_CHECK(!toml::parse_file(f2.string())["calendar"]["account"]["work"]);
  }

  void testValidationAndBatchRollback() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-rollback"});
    const nythera::test::ScopedEnvVar waylandDisplay("WAYLAND_DISPLAY", "nythera-shell-config-test");
    const fs::path shellDir = env.config() / "nythera" / "shell";
    const fs::path base = shellDir / "00-base.toml";
    const fs::path lockedDir = env.work() / "locked";
    const fs::path second = lockedDir / "override.toml";
    const fs::path link = shellDir / "10-override.toml";

    constexpr std::string_view kBaseOriginal =
        "# Keep base comment\n[calendar.account.work]\ntype = \"ics\"\nserver_url = \"https://a.example/old.ics\"\n";
    constexpr std::string_view kSecondOriginal =
        "# Keep second comment\n[calendar.account.work]\nserver_url = \"https://b.example/new.ics\"\n";

    writeFile(base, kBaseOriginal);
    writeFile(second, kSecondOriginal);
    fs::create_symlink(second, link);

    ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
    IpcService ipc;
    config.registerIpc(ipc);

    // Schema validation rejects invalid widget timezone before any write.
    TEST_CHECK(config.setOverride({"widget", "clock", "type"}, std::string("clock")));
    TEST_CHECK(!config.setOverride({"widget", "clock", "timezone"}, std::string("Invalid/NoSuchZone")));
    TEST_CHECK(!config.lastMutationError().empty());
    TEST_CHECK(!toml::parse_file((shellDir / "config.toml").string())["widget"]["clock"]["timezone"]);

    // IPC config-set rejects warnings (unknown setting) without writing.
    const std::string ipcErr = ipc.execute("config-set accessibility.no_such_key 2.0");
    TEST_CHECK(ipcErr.starts_with("error: ") && ipcErr.contains("unknown setting"));
    TEST_CHECK(ipc.execute("config-set accessibility.ui_scale 1.2") == "ok\n");
    TEST_CHECK(config.config().accessibility.uiScale == 1.2F);

    // Multi-file atomic batch rollback: first file writes, second fails because lockedDir is read-only.
    fs::permissions(base, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    const fs::perms basePerms = fs::status(base).permissions();
    const fs::perms lockedDirPerms = fs::status(lockedDir).permissions();
    fs::permissions(lockedDir, fs::perms::owner_read | fs::perms::owner_exec);

    const bool deleted = config.deleteCalendarAccountOverride("work");
    fs::permissions(lockedDir, lockedDirPerms);

    TEST_CHECK(!deleted);
    TEST_CHECK(!config.lastMutationError().empty());
    TEST_CHECK(readFile(base) == kBaseOriginal);
    TEST_CHECK(readFile(second) == kSecondOriginal);
    TEST_CHECK(fs::status(base).permissions() == basePerms);
    TEST_CHECK(config.config().calendar.accounts.size() == 1);
  }

  void testLegacyCompatibilityAndAuthoritativeSwitch() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-legacy"});
    const fs::path legacyConfig = env.config() / "nythera" / "config.toml";
    const fs::path legacySettings = env.state() / "nythera" / "settings.toml";
    const fs::path shellConfig = env.config() / "nythera" / "shell" / "config.toml";

    constexpr std::string_view kLegacyConfig = "[accessibility]\nui_scale = 1.1\n";
    constexpr std::string_view kLegacySettings = "config_version = 14\n[dock]\nenabled = true\n";
    writeFile(legacyConfig, kLegacyConfig);
    writeFile(legacySettings, kLegacySettings);

    {
      ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
      TEST_CHECK(config.activeConfigDir() == env.config() / "nythera");
      TEST_CHECK(config.config().accessibility.uiScale == 1.1F);
      TEST_CHECK(config.config().dock.enabled);
      TEST_CHECK(readFile(legacySettings) == kLegacySettings);
      TEST_CHECK(!fs::exists(shellConfig));

      const auto diag = nythera::config::validateConfigSources(
          ConfigService::resolveActiveConfigLocation().configDir,
          ConfigService::resolveActiveConfigLocation().legacySettingsPath
      );
      TEST_CHECK(!diag.hasErrors());
    }

    // Once ~/.config/nythera/shell/ contains a TOML file, it becomes authoritative.
    writeFile(shellConfig, "[accessibility]\nui_scale = 1.4\n");
    {
      ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
      TEST_CHECK(config.activeConfigDir() == env.config() / "nythera" / "shell");
      TEST_CHECK(config.config().accessibility.uiScale == 1.4F);
      TEST_CHECK(!config.config().dock.enabled);
    }
  }

  void testStructuralMutationsAndInotify() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-structural"});
    const fs::path shellDir = env.config() / "nythera" / "shell";
    const fs::path configPath = shellDir / "config.toml";

    ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
    int reloadCount = 0;
    config.addReloadCallback([&]() { ++reloadCount; }, "structural-test");

    // Bars & monitor overrides: create, move, rename, delete.
    TEST_CHECK(config.createBarOverride("secondary"));
    TEST_CHECK(config.config().bars.size() == 2);
    TEST_CHECK(config.moveBarOverride("secondary", -1));
    TEST_CHECK(config.config().bars.at(0).name == "secondary");
    TEST_CHECK(config.createMonitorOverride("secondary", "DP-1"));
    TEST_CHECK(config.setOverride({"bar", "secondary", "monitor", "DP-1", "thickness"}, std::int64_t{46}));
    TEST_CHECK(config.renameMonitorOverride("secondary", "DP-1", "HDMI-2"));
    TEST_CHECK(config.renameBarOverride("secondary", "aux"));
    TEST_CHECK(config.config().bars.at(0).name == "aux");
    TEST_CHECK(findBarMonitor(config.config().bars.at(0), "HDMI-2")->thickness == 46);
    TEST_CHECK(config.deleteBarOverride("aux"));
    TEST_CHECK(config.config().bars.size() == 1 && config.config().bars.at(0).name == "default");

    // Theme, dock, plugins, idle behaviors, and desktop widgets direct persistence.
    const fs::path sharedThemePath = env.config() / "nythera" / "theme.toml";
    config.setThemeMode(ThemeMode::Light);
    TEST_CHECK(config.setThemeColorScheme(PaletteSource::Builtin, "Ayu"));
    config.setDockEnabled(true);
    config.setPluginsAutoUpdate(PluginAutoUpdateMode::Official);
    config.setPluginEnabled("nythera/demo", true);
    TEST_CHECK(config.config().theme.mode == ThemeMode::Light);
    TEST_CHECK(config.config().theme.builtinPalette == "Ayu");
    TEST_CHECK(fs::exists(sharedThemePath));
    TEST_CHECK(toml::parse_file(sharedThemePath.string())["theme"]["mode"].value<std::string>() == "light");
    TEST_CHECK(toml::parse_file(sharedThemePath.string())["theme"]["builtin"].value<std::string>() == "Ayu");
    TEST_CHECK(!toml::parse_file(configPath.string()).contains("theme"));
    TEST_CHECK(config.config().dock.enabled);
    TEST_CHECK(config.config().plugins.autoUpdate == PluginAutoUpdateMode::Official);
    TEST_CHECK(config.config().plugins.enabled == std::vector<std::string>{"nythera/demo"});

    IdleBehaviorConfig dim{
        .name = "dim", .enabled = true, .timeoutSeconds = 90.0, .action = "dim", .command = "brightnessctl set 20%"
    };
    TEST_CHECK(config.setOverride({"idle", "behavior"}, std::vector<IdleBehaviorConfig>{dim}));
    TEST_CHECK(
        config.config().idle.behaviors.size() == 1
        && config.config().idle.behaviors[0].command == "brightnessctl set 20%"
    );
    // Clearing command removes it from disk instead of leaving stale key behind.
    dim.command.clear();
    TEST_CHECK(config.setOverride({"idle", "behavior"}, std::vector<IdleBehaviorConfig>{dim}));
    TEST_CHECK(config.config().idle.behaviors[0].command.empty());
    TEST_CHECK(!toml::parse_file(configPath.string())["idle"]["behavior"]["dim"]["command"]);

    DesktopWidgetsConfig dw;
    dw.grid.visible = true;
    dw.grid.cellSize = 32;
    dw.widgets.push_back(
        DesktopWidgetState{.id = "clk1", .type = "clock", .outputName = "DP-1", .cx = 100.0F, .cy = 200.0F}
    );
    TEST_CHECK(config.setDesktopWidgetsState(dw));
    TEST_CHECK(config.config().desktopWidgets.widgets.size() == 1);
    dw.widgets.clear();
    TEST_CHECK(config.setDesktopWidgetsState(dw));
    TEST_CHECK(config.config().desktopWidgets.widgets.empty());
    TEST_CHECK(!toml::parse_file(configPath.string())["desktop_widgets"]["widget"]["clk1"]);

    // Self-write echo suppression: draining watchFd after internal writes must not fire extra reloads.
    const int reloadsBeforePoll = reloadCount;
    pollfd pfd{.fd = config.watchFd(), .events = POLLIN, .revents = 0};
    if (pfd.fd >= 0 && ::poll(&pfd, 1, 50) > 0 && (pfd.revents & POLLIN) != 0) {
      config.checkReload();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(170));
    TimerManager::instance().tick();
    TEST_CHECK(reloadCount == reloadsBeforePoll);

    // External write to config.toml IS detected via watchFd and triggers reload.
    const fs::path tempSwap = env.work() / "external-swap.toml";
    writeFile(tempSwap, "[accessibility]\nui_scale = 1.6\n");
    fs::rename(tempSwap, configPath);
    pfd.revents = 0;
    TEST_CHECK(pfd.fd >= 0 && ::poll(&pfd, 1, 500) == 1 && (pfd.revents & POLLIN) != 0);
    config.checkReload();
    std::this_thread::sleep_for(std::chrono::milliseconds(170));
    TimerManager::instance().tick();
    TEST_CHECK(reloadCount == reloadsBeforePoll + 1);
    TEST_CHECK(config.config().accessibility.uiScale == 1.6F);
  }

  void testSharedThemeOwnershipAndPrecedence() {
    // 1. Fresh setup routes global [theme] writes to ~/.config/nythera/theme.toml without
    //    creating shell/ or triggering legacy mode, and routes non-theme keys to shell/config.toml.
    {
      const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-shared-theme-fresh"});
      const fs::path nytheraDir = env.config() / "nythera";
      const fs::path themePath = nytheraDir / "theme.toml";
      const fs::path shellConfig = nytheraDir / "shell" / "config.toml";

      ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
      TEST_CHECK(config.sharedThemeConfigPath() == themePath);
      config.setThemeMode(ThemeMode::Light);
      TEST_CHECK(config.config().theme.mode == ThemeMode::Light);
      TEST_CHECK(fs::exists(themePath));
      TEST_CHECK(!fs::exists(nytheraDir / "shell"));
      TEST_CHECK(toml::parse_file(themePath.string())["theme"]["mode"].value<std::string>() == "light");

      // Re-opening when only theme.toml exists keeps split layout active (not legacy mode).
      ConfigService reopened{
          nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()
      };
      TEST_CHECK(reopened.activeConfigDir() == nytheraDir / "shell");
      TEST_CHECK(reopened.sharedThemeConfigPath() == themePath);
      TEST_CHECK(reopened.config().theme.mode == ThemeMode::Light);

      reopened.setDockEnabled(true);
      TEST_CHECK(fs::exists(shellConfig));
      TEST_CHECK(toml::parse_file(shellConfig.string())["dock"]["enabled"].value<bool>() == true);
      TEST_CHECK(!toml::parse_file(shellConfig.string()).contains("theme"));
    }

    // 2. Shared theme.toml symlink + [include] + comments + live reload + Shell override + reset.
    {
      const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-shared-theme-links"});
      const fs::path nytheraDir = env.config() / "nythera";
      const fs::path themePath = nytheraDir / "theme.toml";
      const fs::path shellConfig = nytheraDir / "shell" / "config.toml";
      const fs::path themeInclude = env.work() / "theme-include.toml";
      const fs::path realTheme = env.work() / "real-theme.toml";

      writeFile(themeInclude, "# Included palette\n[theme]\nbuiltin = \"Ayu\"\n");
      writeFile(
          realTheme,
          "# Shared theme header\n[include]\nfiles = [\"../../work/theme-include.toml\"]\n\n[theme]\nmode = \"light\"\n"
      );
      fs::create_directories(nytheraDir);
      fs::create_symlink("../../work/real-theme.toml", themePath);

      ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
      TEST_CHECK(config.config().theme.mode == ThemeMode::Light);
      TEST_CHECK(config.config().theme.builtinPalette == "Ayu");

      // New key inside shared [theme] routes to winning table's file (through theme.toml symlink),
      // preserving comments and symlink integrity without creating shell/.
      TEST_CHECK(config.setOverride({"theme", "pure_black_dark"}, true));
      TEST_CHECK(fs::is_symlink(themePath));
      TEST_CHECK(!fs::exists(shellConfig));
      const std::string realContent = readFile(realTheme);
      TEST_CHECK(realContent.contains("# Shared theme header"));
      TEST_CHECK(toml::parse_file(realTheme.string())["theme"]["pure_black_dark"].value<bool>() == true);

      // Shell config under shell/ overrides shared theme.toml; winning edits hit shell/config.toml;
      // reset removes across both shared theme.toml and shell/config.toml.
      writeFile(shellConfig, "# Shell override\n[theme]\nmode = \"dark\"\n");
      config.forceReload();
      TEST_CHECK(config.config().theme.mode == ThemeMode::Dark);
      TEST_CHECK(config.config().theme.builtinPalette == "Ayu");
      config.setThemeMode(ThemeMode::Auto);
      TEST_CHECK(toml::parse_file(shellConfig.string())["theme"]["mode"].value<std::string>() == "auto");
      TEST_CHECK(toml::parse_file(realTheme.string())["theme"]["mode"].value<std::string>() == "light");
      TEST_CHECK(config.clearOverride({"theme", "mode"}));
      TEST_CHECK(config.config().theme.mode == ThemeMode::Dark);
      TEST_CHECK(!toml::parse_file(shellConfig.string())["theme"]["mode"]);
      TEST_CHECK(!toml::parse_file(realTheme.string())["theme"]["mode"]);
      TEST_CHECK(readFile(shellConfig).contains("# Shell override"));
      TEST_CHECK(readFile(realTheme).contains("# Shared theme header"));

      // Files' theme.toml stays completely isolated from Shell / shared theme & templates.
      const fs::path filesTheme = nytheraDir / "files" / "theme.toml";
      constexpr std::string_view kFilesTheme = "[theme]\ntransparency_mode = \"soft\"\n";
      writeFile(filesTheme, kFilesTheme);
      TEST_CHECK(config.setOverride({"theme", "templates", "enable_builtin_templates"}, false));
      TEST_CHECK(
          toml::parse_file(realTheme.string())["theme"]["templates"]["enable_builtin_templates"].value<bool>() == false
      );
      TEST_CHECK(readFile(filesTheme) == kFilesTheme);

      // Merged export, support report, and CLI validation all include shared theme.toml.
      const std::string merged = config.buildMergedUserConfig(nythera::config::CredentialExportMode::Keep);
      TEST_CHECK(toml::parse(merged)["theme"]["pure_black_dark"].value<bool>() == true);
      const std::string report = config.buildSupportReport();
      const auto reportTbl = toml::parse(report);
      TEST_CHECK(reportTbl["paths"]["theme_path"].value<std::string>() == themePath.string());
      bool sawThemeSource = false;
      if (const auto* srcs = reportTbl["config_sources"].as_array()) {
        for (const auto& entry : *srcs) {
          if (const auto* tbl = entry.as_table()) {
            if ((*tbl)["path"].value<std::string>() == themePath.string()) {
              sawThemeSource = true;
            }
          }
        }
      }
      TEST_CHECK(sawThemeSource);

      const auto loc = ConfigService::resolveActiveConfigLocation();
      const auto validDiag =
          nythera::config::validateConfigSources(loc.configDir, loc.legacySettingsPath, loc.sharedThemePath);
      TEST_CHECK(validDiag.entries.empty());

      // External update through theme.toml is detected via watchFd and reloads cleanly.
      int reloadCount = 0;
      config.addReloadCallback([&]() { ++reloadCount; }, "shared-theme-watch");
      writeFile(realTheme, "[theme]\nmode = \"light\"\npure_black_dark = false\n");
      pollfd pfd{.fd = config.watchFd(), .events = POLLIN, .revents = 0};
      TEST_CHECK(pfd.fd >= 0 && ::poll(&pfd, 1, 500) == 1 && (pfd.revents & POLLIN) != 0);
      config.checkReload();
      std::this_thread::sleep_for(std::chrono::milliseconds(170));
      TimerManager::instance().tick();
      TEST_CHECK(reloadCount == 1);
      TEST_CHECK(config.config().theme.mode == ThemeMode::Light);
      TEST_CHECK(!config.config().theme.pureBlackDark);
    }

    // 3. Cross-file selection: applyWallpaperSelection with preset writes [theme] to theme.toml
    //    and active wallpaper paths to state/nythera/shell/state.toml without creating shell/config.toml.
    {
      const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-shared-theme-crossfile"});
      const fs::path nytheraDir = env.config() / "nythera";
      const fs::path themePath = nytheraDir / "theme.toml";
      const fs::path shellConfig = nytheraDir / "shell" / "config.toml";
      const fs::path shellState = env.state() / "nythera" / "shell" / "state.toml";

      ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
      WallpaperFavorite preset{
          .path = "/tmp/wp.png",
          .themeMode = ThemeMode::Light,
          .paletteSource = PaletteSource::Builtin,
          .builtinPalette = "Ayu",
      };
      config.applyWallpaperSelection(std::nullopt, "/tmp/wp.png", &preset, {});
      TEST_CHECK(config.config().theme.mode == ThemeMode::Light);
      TEST_CHECK(config.config().theme.builtinPalette == "Ayu");
      TEST_CHECK(config.getWallpaperPath("DP-1") == "/tmp/wp.png");
      const auto themeTbl = toml::parse_file(themePath.string());
      const auto stateTbl = toml::parse_file(shellState.string());
      TEST_CHECK(themeTbl["theme"]["mode"].value<std::string>() == "light");
      TEST_CHECK(themeTbl["theme"]["builtin"].value<std::string>() == "Ayu");
      TEST_CHECK(!themeTbl.contains("wallpaper"));
      TEST_CHECK(!fs::exists(shellConfig));
      TEST_CHECK(stateTbl["wallpaper"]["default"].value<std::string>() == "/tmp/wp.png");
      TEST_CHECK(stateTbl["wallpaper"]["last"].value<std::string>() == "/tmp/wp.png");
    }
  }

  void testShellStateDirectoryAndLegacyStateCompatibility() {
    // 1. Authoritative Shell runtime state ownership under ~/.local/state/nythera/shell/,
    //    active/per-monitor wallpaper persistence in state.toml, and wallpaper config preferences
    //    staying in shell/config.toml.
    {
      const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-state-dir"});
      const fs::path shellConfig = env.config() / "nythera" / "shell" / "config.toml";
      const fs::path legacyState = env.state() / "nythera" / "state.toml";
      const fs::path shellState = env.state() / "nythera" / "shell" / "state.toml";
      const fs::path setupMarker = env.state() / "nythera" / "shell" / ".setup-complete";

      writeFile(
          shellConfig,
          "[wallpaper]\nenabled = true\ndirectory = \"/walls\"\ntransition_on_startup = true\n"
          "[wallpaper.automation]\nenabled = true\ninterval_seconds = 900\n"
      );
      TEST_CHECK(ConfigService::resolveActiveStateDir() == (env.state() / "nythera" / "shell").string());

      ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
      int wallpaperCallbacks = 0;
      config.addReloadCallback([] {}, "noop");
      config.setWallpaperChangeCallback([&]() { ++wallpaperCallbacks; });
      TEST_CHECK(config.shouldRunSetupWizard());

      config.setWallpaperPath(std::nullopt, "/walls/default.png");
      config.setWallpaperPath("DP-1", "/walls/dp1.png");
      TEST_CHECK(wallpaperCallbacks == 2);
      TEST_CHECK(fs::exists(shellState));
      TEST_CHECK(!fs::exists(legacyState));
      TEST_CHECK(config.getWallpaperPath("DP-1") == "/walls/dp1.png");
      TEST_CHECK(config.getWallpaperPath("HDMI-1") == "/walls/default.png");
      TEST_CHECK(config.getPaletteWallpaperPath() == "/walls/dp1.png");

      TEST_CHECK(config.setStateBool("wallpaper_panel", "flatten", true));
      TEST_CHECK(config.markSetupWizardCompleted());
      TEST_CHECK(fs::exists(setupMarker));
      TEST_CHECK(!config.shouldRunSetupWizard());

      // Config preferences stay untouched in shell/config.toml without [wallpaper.default]/[wallpaper.last].
      const auto cfgTbl = toml::parse_file(shellConfig.string());
      TEST_CHECK(cfgTbl["wallpaper"]["transition_on_startup"].value<bool>() == true);
      TEST_CHECK(cfgTbl["wallpaper"]["automation"]["interval_seconds"].value<int>() == 900);
      TEST_CHECK(!cfgTbl["wallpaper"]["default"]);
      TEST_CHECK(!cfgTbl["wallpaper"]["last"]);
      TEST_CHECK(!cfgTbl["wallpaper"]["monitors"]);

      // Re-opening restores state-stored wallpaper selections, setup completion, and panel state.
      ConfigService reopened{
          nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()
      };
      TEST_CHECK(!reopened.shouldRunSetupWizard());
      TEST_CHECK(reopened.stateBool("wallpaper_panel", "flatten") == true);
      TEST_CHECK(reopened.getWallpaperPath("DP-1") == "/walls/dp1.png");
      TEST_CHECK(reopened.getDefaultWallpaperPath() == "/walls/default.png");
      TEST_CHECK(reopened.getPaletteWallpaperPath() == "/walls/dp1.png");
      TEST_CHECK(reopened.config().wallpaper.transitionOnStartup);
      TEST_CHECK(reopened.config().wallpaper.automation.enabled);
      TEST_CHECK(reopened.config().wallpaper.automation.intervalSeconds == 900);
      TEST_CHECK(reopened.config().wallpaper.directory == "/walls");
    }

    // 2. Unmigrated live compatibility: legacy config without shell/ keeps using ~/.local/state/nythera/
    //    and respects legacy .setup-complete and settings.toml without migrating them.
    {
      const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-legacy-state"});
      const fs::path legacyConfig = env.config() / "nythera" / "config.toml";
      const fs::path legacySettings = env.state() / "nythera" / "settings.toml";
      const fs::path legacyState = env.state() / "nythera" / "state.toml";
      const fs::path legacyMarker = env.state() / "nythera" / ".setup-complete";

      writeFile(legacyConfig, "[wallpaper]\ntransition_on_startup = true\n");
      writeFile(legacySettings, "config_version = 14\n[wallpaper.default]\npath = \"/legacy/wall.png\"\n");
      writeFile(legacyMarker, "");

      TEST_CHECK(ConfigService::resolveActiveStateDir() == (env.state() / "nythera").string());
      ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};
      TEST_CHECK(!config.shouldRunSetupWizard());
      TEST_CHECK(config.getDefaultWallpaperPath() == "/legacy/wall.png");
      TEST_CHECK(!fs::exists(env.state() / "nythera" / "shell"));

      config.setWallpaperPath("DP-2", "/legacy/dp2.png");
      TEST_CHECK(fs::exists(legacyState));
      TEST_CHECK(!fs::exists(env.state() / "nythera" / "shell"));
      TEST_CHECK(
          toml::parse_file(legacyState.string())["wallpaper"]["monitors"]["DP-2"].value<std::string>()
          == "/legacy/dp2.png"
      );
      TEST_CHECK(readFile(legacySettings).contains("/legacy/wall.png"));
    }
  }

  void testSettingsIndicatorsCrudAndLiveReloadGate() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-shell-stage4-settings"});
    const fs::path nytheraDir = env.config() / "nythera";
    const fs::path themePath = nytheraDir / "theme.toml";
    const fs::path shellConfig = nytheraDir / "shell" / "config.toml";
    const fs::path barsFile = nytheraDir / "shell" / "extra" / "bars.toml";
    const fs::path widgetsFile = nytheraDir / "shell" / "extra" / "widgets.toml";

    writeFile(themePath, "[theme]\nmode = \"dark\"\npure_black_dark = true\n");
    writeFile(
        shellConfig,
        "[include]\nfiles = [\"extra/bars.toml\", \"extra/widgets.toml\"]\n"
        "[bar.default]\nauto_hide = true\nthickness = 48\n"
    );
    writeFile(
        barsFile,
        "[bar.default]\nthickness = 48\nstart = [\"custom_clock\"]\n"
        "[bar.default.monitor.\"DP-1\"]\nscale = 1.5\n"
        "[dock.monitor.\"HDMI-A-1\"]\nmatch = \"HDMI-A-1\"\nreserve_space = false\n"
    );
    writeFile(
        widgetsFile,
        "[widget.custom_clock]\ntype = \"clock\"\nformat = \"%H:%M\"\n"
        "[calendar.account.manual_work]\nprovider = \"ics\"\nname = \"Work\"\nserver_url = "
        "\"https://example.com/work.ics\"\n"
    );

    ConfigService config{nythera::config::shell::validator(), nythera::config::shell::overrideEffectivenessChecker()};

    // 1. Modified indicators reflect differences from built-in defaults across files.
    TEST_CHECK(!config.hasEffectiveOverride({"theme", "mode"})); // matches built-in "dark"
    TEST_CHECK(config.hasEffectiveOverride({"theme", "pure_black_dark"}));
    TEST_CHECK(config.validateOverride({"theme", "mode"}, ConfigOverrideValue{std::string("light")}));
    TEST_CHECK(config.hasEffectiveOverride({"bar", "default", "auto_hide"}));
    TEST_CHECK(config.hasEffectiveOverride({"bar", "default", "thickness"}));
    TEST_CHECK(config.hasEffectiveBarLaneOverride({"bar", "default", "start"}));
    TEST_CHECK(config.hasEffectiveOverride({"widget", "custom_clock", "format"}));

    bool changed = false;
    TEST_CHECK(config.setOverride({"theme", "mode"}, std::string("light"), &changed));
    TEST_CHECK(changed && config.hasEffectiveOverride({"theme", "mode"}));
    TEST_CHECK(config.setOverride({"theme", "mode"}, std::string("dark"), &changed));
    TEST_CHECK(changed && !config.hasEffectiveOverride({"theme", "mode"}));

    // 2. Multi-file reset removes all contributing definitions and restores built-in defaults.
    changed = false;
    TEST_CHECK(config.clearOverrides({{"bar", "default", "thickness"}, {"theme", "pure_black_dark"}}, &changed));
    TEST_CHECK(changed);
    TEST_CHECK(config.config().bars.front().thickness == BarConfig{}.thickness);
    TEST_CHECK(!config.config().theme.pureBlackDark);
    TEST_CHECK(!config.hasEffectiveOverride({"bar", "default", "thickness"}));
    TEST_CHECK(!config.hasEffectiveOverride({"theme", "pure_black_dark"}));
    TEST_CHECK(!toml::parse_file(shellConfig.string())["bar"]["default"]["thickness"]);
    TEST_CHECK(!toml::parse_file(barsFile.string())["bar"]["default"]["thickness"]);
    TEST_CHECK(!toml::parse_file(themePath.string())["theme"]["pure_black_dark"]);

    // 3. Manually configured entities support full CRUD without override-only restrictions.
    TEST_CHECK(config.isOverrideOnlyBar("default"));
    TEST_CHECK(config.isOverrideOnlyMonitorOverride("default", "DP-1"));
    TEST_CHECK(config.isOverrideOnlyDockMonitorOverride("HDMI-A-1"));
    TEST_CHECK(config.isOverrideOnlyCalendarAccount("manual_work"));
    TEST_CHECK(config.hasOverride({"widget", "custom_clock"}));

    // Invalid batch rollback keeps [widget.custom_clock] and lane references intact.
    TEST_CHECK(!config.renameOverrideTable(
        {"widget", "custom_clock"}, {"widget", "main_clock"},
        {{{"bar", "default", "start"}, std::vector<std::string>{"main_clock"}},
         {{"widget", "clock", "timezone"}, std::string("Invalid/NoSuchZone")}}
    ));
    TEST_CHECK(config.hasOverride({"widget", "custom_clock"}));
    TEST_CHECK(!config.hasOverride({"widget", "main_clock"}));

    // Valid atomic widget rename updates widget definition and lane reference in one commit.
    TEST_CHECK(config.renameOverrideTable(
        {"widget", "custom_clock"}, {"widget", "main_clock"},
        {{{"bar", "default", "start"}, std::vector<std::string>{"main_clock"}}}
    ));
    TEST_CHECK(!config.hasOverride({"widget", "custom_clock"}));
    TEST_CHECK(config.hasOverride({"widget", "main_clock"}));
    TEST_CHECK(
        toml::parse_file(widgetsFile.string())["widget"]["main_clock"]["format"].value<std::string>() == "%H:%M"
    );
    TEST_CHECK(toml::parse_file(barsFile.string())["bar"]["default"]["start"][0].value<std::string>() == "main_clock");

    // Atomic widget delete clears reference and removes [widget.<name>] together.
    changed = false;
    TEST_CHECK(config.mutateOverrides(
        {{{"bar", "default", "start"}, std::vector<std::string>{}}}, {{"widget", "main_clock"}}, &changed
    ));
    TEST_CHECK(changed && !config.hasOverride({"widget", "main_clock"}));
    TEST_CHECK(config.config().bars.front().startWidgets.empty());

    TEST_CHECK(config.deleteCalendarAccountOverride("manual_work"));
    TEST_CHECK(!config.isOverrideOnlyCalendarAccount("manual_work"));
    TEST_CHECK(config.renameDockMonitorOverride("HDMI-A-1", "HDMI-A-2"));
    TEST_CHECK(config.deleteDockMonitorOverride("HDMI-A-2"));
    TEST_CHECK(config.renameMonitorOverride("default", "DP-1", "eDP-1"));
    TEST_CHECK(config.deleteMonitorOverride("default", "eDP-1"));

    // 4. ExternalReloadGate ignores self-mutations, defers during active edits, and flushes on completion.
    nythera::settings::ExternalReloadGate gate;
    bool windowOpen = true;
    bool activeUserEdit = true;
    int appliedReloads = 0;
    int uiRenderedThickness = config.config().bars.front().thickness;
    config.addReloadCallback(
        [&]() {
          if (gate.onExternalReload(windowOpen, activeUserEdit)) {
            ++appliedReloads;
            uiRenderedThickness = config.config().bars.front().thickness;
          }
        },
        "settings-stage4-test"
    );

    {
      const auto ownMutation = gate.scopedOwnMutation();
      TEST_CHECK(config.setOverride({"bar", "default", "thickness"}, std::int64_t(38)));
    }
    TEST_CHECK(appliedReloads == 0);
    TEST_CHECK(!gate.hasPendingReload());
    uiRenderedThickness = config.config().bars.front().thickness;
    TEST_CHECK(uiRenderedThickness == 38);

    // Manual TOML edit arrives while user is actively editing: defer without overwriting UI state.
    writeFile(
        shellConfig,
        "[include]\nfiles = [\"extra/bars.toml\", \"extra/widgets.toml\"]\n"
        "[bar.default]\nthickness = 44\n"
    );
    pollfd pfd{.fd = config.watchFd(), .events = POLLIN, .revents = 0};
    TEST_CHECK(pfd.fd >= 0 && ::poll(&pfd, 1, 500) == 1 && (pfd.revents & POLLIN) != 0);
    config.checkReload();
    std::this_thread::sleep_for(std::chrono::milliseconds(170));
    TimerManager::instance().tick();
    TEST_CHECK(config.config().bars.front().thickness == 44);
    TEST_CHECK(appliedReloads == 0);
    TEST_CHECK(uiRenderedThickness == 38);
    TEST_CHECK(gate.hasPendingReload());
    TEST_CHECK(!gate.flushIfReady(windowOpen, activeUserEdit));

    // Finishing the active user edit flushes the deferred external reload.
    activeUserEdit = false;
    TEST_CHECK(gate.flushIfReady(windowOpen, activeUserEdit));
    appliedReloads += 1;
    uiRenderedThickness = config.config().bars.front().thickness;
    TEST_CHECK(!gate.hasPendingReload());
    TEST_CHECK(uiRenderedThickness == 44);

    // Subsequent valid external TOML edit while idle updates open Settings immediately.
    writeFile(
        shellConfig,
        "[include]\nfiles = [\"extra/bars.toml\", \"extra/widgets.toml\"]\n"
        "[bar.default]\nthickness = 50\n"
    );
    pfd = {.fd = config.watchFd(), .events = POLLIN, .revents = 0};
    TEST_CHECK(::poll(&pfd, 1, 500) == 1 && (pfd.revents & POLLIN) != 0);
    config.checkReload();
    std::this_thread::sleep_for(std::chrono::milliseconds(170));
    TimerManager::instance().tick();
    TEST_CHECK(appliedReloads == 2);
    TEST_CHECK(uiRenderedThickness == 50);
  }

} // namespace

int main() {
  return nythera::test::runTest([] {
    testSparseDefaultsAndFirstWrite();
    testIncludesPrecedenceAndSessionActionMerging();
    testResetAndEntityDeletionAcrossFiles();
    testValidationAndBatchRollback();
    testLegacyCompatibilityAndAuthoritativeSwitch();
    testStructuralMutationsAndInotify();
    testSharedThemeOwnershipAndPrecedence();
    testShellStateDirectoryAndLegacyStateCompatibility();
    testSettingsIndicatorsCrudAndLiveReloadGate();
  });
}
