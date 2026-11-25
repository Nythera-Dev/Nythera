// Locks the settings GUI's lane override and reset signal across capsule-group edits under
// direct configuration persistence: adding a widget to a lane and folding it into a capsule
// group keeps the lane reporting as customized, and resetting the lane removes its definition
// and any groups it owned while preserving groups used by other lanes.

#include "config/config_service.h"
#include "config/config_types.h"
#include "test_isolation.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace {

  int g_failures = 0;

  void expect(bool condition, const char* message) {
    if (!condition) {
      std::println(stderr, "capsule_group_lane_override: FAIL: {}", message);
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
  const nythera::test::ScopedTestEnv env({.prefix = "nythera-lane-override"});
  const auto configPath = env.config() / "nythera" / "shell" / "config.toml";
  const auto legacySettings = env.state() / "nythera" / "settings.toml";

  const std::vector<std::string> lanePath{"bar", "default", "start"};
  const std::vector<std::string> groupPath{"bar", "default", "capsule_group"};

  {
    writeFile(configPath, R"(
[bar.default]
start = [ "clock", "weather" ]
)");
    ConfigService config;

    // Add a widget to the lane: the lane is now customized.
    expect(config.setOverride(lanePath, std::vector<std::string>{"clock", "weather", "volume"}), "add widget writes");
    expect(!std::filesystem::exists(legacySettings), "direct persistence must not create settings.toml");
    expect(config.hasOverride(lanePath), "lane definition stored after add");
    expect(config.hasEffectiveOverride(lanePath), "lane override effective after add");

    // Fold the added widget into a capsule group with its neighbour.
    BarCapsuleGroupStyle group;
    group.id = "g1";
    group.members = {"weather", "volume"};
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> grouping;
    grouping.emplace_back(lanePath, std::vector<std::string>{"clock", "group:g1"});
    grouping.emplace_back(groupPath, std::vector<BarCapsuleGroupStyle>{group});
    expect(config.setOverrides(std::move(grouping)), "grouping writes");

    expect(config.hasOverride(lanePath), "lane definition stored after grouping");
    expect(config.hasEffectiveOverride(lanePath), "lane override effective after grouping");
    expect(config.hasEffectiveOverride(groupPath), "group override effective after grouping");

    const auto& bars = config.config().bars;
    expect(
        bars.size() == 1 && bars[0].startWidgets == std::vector<std::string>{"clock", "group:g1"},
        "lane resolves to the grouped token"
    );
  }

  // Folding a widget into an existing group keeps the lane customized relative to built-in defaults
  // while leaving an untouched lane clean.
  {
    writeFile(configPath, R"(
[bar.default]
start = [ "clock", "group:g1" ]

[[bar.default.capsule_group]]
id = "g1"
members = [ "network", "bluetooth" ]
)");
    ConfigService config;

    expect(
        config.setOverride(lanePath, std::vector<std::string>{"clock", "group:g1", "volume"}),
        "add widget writes (file group)"
    );
    expect(config.hasEffectiveOverride(lanePath), "lane override effective after add (file group)");
    expect(config.hasEffectiveBarLaneOverride(lanePath), "lane content overridden after add (file group)");

    BarCapsuleGroupStyle group;
    group.id = "g1";
    group.members = {"network", "bluetooth", "volume"};
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> grouping;
    grouping.emplace_back(lanePath, std::vector<std::string>{"clock", "group:g1"});
    grouping.emplace_back(groupPath, std::vector<BarCapsuleGroupStyle>{group});
    expect(config.setOverrides(std::move(grouping)), "grouping writes (file group)");

    expect(config.hasEffectiveOverride(lanePath), "customized start lane stays reported as overridden");
    expect(config.hasEffectiveOverride(groupPath), "group override effective after grouping (file group)");
    expect(config.hasEffectiveBarLaneOverride(lanePath), "lane content overridden through its group");

    // An untouched lane matching the built-in default is not overridden.
    expect(!config.hasEffectiveBarLaneOverride({"bar", "default", "end"}), "untouched lane is not overridden");
  }

  // Resetting a lane removes its list definition and drops groups owned exclusively by that lane,
  // leaving another lane's group edit alone even though both live in the same capsule_group array.
  {
    writeFile(configPath, R"(
[bar.default]
start = [ "clock", "group:g1" ]
end = [ "group:g2" ]

[[bar.default.capsule_group]]
id = "g1"
members = [ "network", "bluetooth" ]

[[bar.default.capsule_group]]
id = "g2"
members = [ "battery", "clock" ]
)");
    ConfigService config;

    BarCapsuleGroupStyle first;
    first.id = "g1";
    first.members = {"network", "bluetooth", "volume"};
    BarCapsuleGroupStyle second;
    second.id = "g2";
    second.members = {"battery", "clock", "cpu"};
    BarCapsuleGroupStyle third;
    third.id = "g3";
    third.members = {"cpu", "memory"};
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> edits;
    edits.emplace_back(lanePath, std::vector<std::string>{"clock", "group:g1", "group:g3"});
    edits.emplace_back(groupPath, std::vector<BarCapsuleGroupStyle>{first, second, third});
    expect(config.setOverrides(std::move(edits)), "lane and groups updated");
    expect(config.hasEffectiveBarLaneOverride(lanePath), "start lane overridden");
    expect(config.hasEffectiveBarLaneOverride({"bar", "default", "end"}), "end lane overridden through g2");

    bool changed = false;
    expect(config.resetBarLaneOverride(lanePath, &changed) && changed, "resetting start writes");
    expect(!config.hasOverride(lanePath), "start lane definition removed after reset");
    expect(!config.hasEffectiveOverride(lanePath), "start lane clean after reset");
    expect(!config.hasEffectiveBarLaneOverride(lanePath), "start lane content clean after reset");
    expect(config.hasEffectiveBarLaneOverride({"bar", "default", "end"}), "end lane keeps its group override");
    expect(
        config.config().bars.at(0).startWidgets == BarConfig{}.startWidgets, "start lane reverted to built-in default"
    );

    const auto& groups = config.config().bars.at(0).widgetCapsuleGroups;
    const auto g1 = std::ranges::find(groups, "g1", &BarCapsuleGroupStyle::id);
    const auto g2 = std::ranges::find(groups, "g2", &BarCapsuleGroupStyle::id);
    expect(g1 == groups.end(), "g1 owned exclusively by start was dropped on reset");
    expect(
        g2 != groups.end() && g2->members == std::vector<std::string>{"battery", "clock", "cpu"}, "g2 keeps its members"
    );
    expect(
        std::ranges::find(groups, "g3", &BarCapsuleGroupStyle::id) == groups.end(),
        "g3 owned exclusively by start was dropped on reset"
    );
  }

  if (g_failures == 0) {
    std::println("capsule_group_lane_override: OK");
  }
  return g_failures == 0 ? 0 : 1;
}
