// Locks that every capsule group style field edited in the settings GUI persists directly
// to user configuration and survives a reload.

#include "config/color_spec.h"
#include "config/config_service.h"
#include "config/config_types.h"
#include "test_isolation.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <vector>

namespace {

  int g_failures = 0;

  void expect(bool condition, const char* message) {
    if (!condition) {
      std::println(stderr, "capsule_group_style_persist: FAIL: {}", message);
      ++g_failures;
    }
  }

  void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << content;
  }

} // namespace

int main() {
  const nythera::test::ScopedTestEnv env({.prefix = "nythera-group-style-persist"});
  const auto configPath = env.config() / "nythera" / "shell" / "config.toml";
  writeFile(configPath, R"(
[bar.default]
start = [ "clock", "weather" ]
)");

  // Every style field differs from its default so a dropped key cannot pass by accident.
  BarCapsuleGroupStyle group;
  group.id = "g1";
  group.members = {"clock", "weather"};
  group.enabled = false;
  group.fill = colorSpecFromConfigString("#223344");
  group.borderSpecified = true;
  group.border = colorSpecFromConfigString("primary");
  group.borderWidth = 4.5F;
  group.foreground = colorSpecFromConfigString("#556677");
  group.padding = 12.0F;
  group.radius = 9.0F;
  group.opacity = 0.5F;
  group.accordion = true;
  group.accordionDirection = BarAccordionDirection::Start;
  group.accordionDurationMs = 325;
  group.accordionDelayMs = 125;
  group.widgetSpacing = 3;

  {
    ConfigService config;
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> edits;
    edits.emplace_back(std::vector<std::string>{"bar", "default", "start"}, std::vector<std::string>{"group:g1"});
    edits.emplace_back(
        std::vector<std::string>{"bar", "default", "capsule_group"}, std::vector<BarCapsuleGroupStyle>{group}
    );
    expect(config.setOverrides(std::move(edits)), "group override writes");
    expect(!std::filesystem::exists(env.state() / "nythera" / "settings.toml"), "must not create settings.toml");
  }

  {
    ConfigService config;
    const auto& bars = config.config().bars;
    expect(bars.size() == 1, "one bar after reload");
    if (bars.size() == 1) {
      const auto& groups = bars[0].widgetCapsuleGroups;
      const auto it = std::ranges::find(groups, "g1", &BarCapsuleGroupStyle::id);
      expect(it != groups.end(), "group present after reload");
      if (it != groups.end()) {
        expect(it->borderWidth == group.borderWidth, "border_width persists");
        expect(*it == group, "every group style field persists");
      }
    }
  }

  if (g_failures == 0) {
    std::println("capsule_group_style_persist: OK");
  }
  return g_failures == 0 ? 0 : 1;
}
