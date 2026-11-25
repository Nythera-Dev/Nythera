#include "config/config_service.h"
#include "config/config_types.h"
#include "core/toml.h"
#include "test_isolation.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <print>
#include <string>
#include <string_view>

namespace {

  int g_failures = 0;

  void expect(bool condition, std::string_view message) {
    if (!condition) {
      std::println(stderr, "config_wallpaper_precedence_test: FAIL: {}", message);
      ++g_failures;
    }
  }

  void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
  }

  std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
  }

  // Check that wallpaper setting from shell/config.toml isn't dropped when another setting
  // is written directly, and that state/settings.toml is never created.
  void checkConfigSurvivesDirectWrite() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-wallpaper-config-file"});
    const auto configPath = env.config() / "nythera" / "shell" / "config.toml";
    const auto legacySidecar = env.state() / "nythera" / "settings.toml";

    writeFile(
        configPath,
        "# Wallpaper selection\n[wallpaper]\nenabled = true\n\n[wallpaper.default]\npath = \"/tmp/from-config.png\"\n"
    );
    expect(!std::filesystem::exists(legacySidecar), "state dir should start without settings.toml");

    ConfigService config;
    expect(config.getDefaultWallpaperPath() == "/tmp/from-config.png", "path was not read from shell/config.toml");

    expect(config.setOverride({"accessibility", "ui_scale"}, 1.25), "setOverride failed");
    expect(!std::filesystem::exists(legacySidecar), "setOverride must not create legacy settings.toml");
    expect(readFile(configPath).contains("# Wallpaper selection"), "comment was lost during direct write");
    expect(
        config.getDefaultWallpaperPath() == "/tmp/from-config.png",
        "shell/config.toml wallpaper path was dropped by direct write"
    );
  }

  void checkDirectWallpaperUpdateAndFallback() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-wallpaper-direct"});
    const auto configPath = env.config() / "nythera" / "shell" / "config.toml";
    const auto shellStatePath = env.state() / "nythera" / "shell" / "state.toml";
    const auto legacyStatePath = env.state() / "nythera" / "state.toml";
    const auto legacySidecar = env.state() / "nythera" / "settings.toml";

    writeFile(configPath, "[wallpaper]\nenabled = true\n\n[wallpaper.default]\npath = \"/tmp/from-config.png\"\n");

    ConfigService config;
    config.setWallpaperPath(std::nullopt, "/tmp/picked.png");
    expect(!std::filesystem::exists(legacySidecar), "setWallpaperPath must not create settings.toml");
    expect(!std::filesystem::exists(legacyStatePath), "setWallpaperPath must write under state/nythera/shell/");
    expect(std::filesystem::exists(shellStatePath), "setWallpaperPath did not write shell/state.toml");
    expect(
        toml::parse_file(configPath.string())["wallpaper"]["default"]["path"].value<std::string>()
            == "/tmp/from-config.png",
        "setWallpaperPath modified shell/config.toml instead of state.toml"
    );
    expect(
        toml::parse_file(shellStatePath.string())["wallpaper"]["default"].value<std::string>() == "/tmp/picked.png",
        "shell/state.toml did not persist default wallpaper path"
    );
    expect(config.getDefaultWallpaperPath() == "/tmp/picked.png", "updated wallpaper path did not take effect");

    config.forceReload();
    expect(config.getDefaultWallpaperPath() == "/tmp/picked.png", "updated wallpaper path did not survive reload");

    config.setWallpaperPath("DP-9", "/tmp/dp9.png");
    config.forceReload();
    expect(config.getWallpaperPath("DP-9") == "/tmp/dp9.png", "per-monitor path did not survive a reload");
    expect(config.getWallpaperPath("DP-8") == "/tmp/picked.png", "unconfigured monitor did not fall back to default");
  }

  // Path fields document that ~ and $VARS expand on read (see field.h
  // pathStringField). Regression guard: wallpaper.directory must expand $VARS.
  void checkDirectoryExpandsEnvVars() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-wallpaper-envvar"});
    const nythera::test::ScopedEnvVar wpEnv("NYTHERA_WP_TEST", "/tmp/wp-root");

    writeFile(
        env.config() / "nythera" / "shell" / "config.toml",
        "[wallpaper]\nenabled = true\ndirectory = \"$NYTHERA_WP_TEST/pics\"\n"
    );

    ConfigService config;
    expect(
        config.config().wallpaper.directory == "/tmp/wp-root/pics",
        "wallpaper.directory did not expand $NYTHERA_WP_TEST"
    );
  }

  // A favorite starred without a preset must not touch the global theme mode when applied;
  // one with an explicit theme_mode must restore it.
  void checkFavoriteThemeModeOverride() {
    const nythera::test::ScopedTestEnv env({.prefix = "nythera-wallpaper-favorite"});
    const auto configPath = env.config() / "nythera" / "shell" / "config.toml";
    const auto shellStatePath = env.state() / "nythera" / "shell" / "state.toml";
    writeFile(
        configPath,
        "[theme]\nmode = \"dark\"\n\n"
        "[[wallpaper.favorite]]\npath = \"/tmp/explicit.png\"\ntheme_mode = \"light\"\n"
    );

    ConfigService config;
    expect(config.config().theme.mode == ThemeMode::Dark, "global theme mode was not read");

    config.addWallpaperFavorite("/tmp/bookmark.png");
    config.forceReload();
    const WallpaperFavorite* bookmark = config.wallpaperFavorite("/tmp/bookmark.png");
    expect(bookmark != nullptr && !bookmark->themeMode.has_value(), "path-only favorite gained a theme mode");

    config.applyWallpaperSelection(std::nullopt, "/tmp/bookmark.png", bookmark, {});
    expect(config.config().theme.mode == ThemeMode::Dark, "path-only favorite changed the global theme mode");
    expect(config.getDefaultWallpaperPath() == "/tmp/bookmark.png", "bookmark wallpaper did not persist to state");

    config.applyWallpaperSelection(
        std::nullopt, "/tmp/explicit.png", config.wallpaperFavorite("/tmp/explicit.png"), {}
    );
    expect(config.config().theme.mode == ThemeMode::Light, "favorite theme_mode was not applied");
    expect(
        toml::parse_file(shellStatePath.string())["wallpaper"]["default"].value<std::string>() == "/tmp/explicit.png",
        "applyWallpaperSelection did not persist default wallpaper in shell/state.toml"
    );
    expect(
        !toml::parse_file(configPath.string())["wallpaper"]["default"],
        "applyWallpaperSelection leaked wallpaper.default into shell/config.toml"
    );
    expect(
        !std::filesystem::exists(env.state() / "nythera" / "settings.toml"), "favorites must not create settings.toml"
    );
  }

} // namespace

int main() {
  checkConfigSurvivesDirectWrite();
  checkDirectWallpaperUpdateAndFallback();
  checkDirectoryExpandsEnvVars();
  checkFavoriteThemeModeOverride();

  if (g_failures == 0) {
    std::println("config_wallpaper_precedence_test: all checks passed");
  }
  return g_failures == 0 ? 0 : 1;
}
