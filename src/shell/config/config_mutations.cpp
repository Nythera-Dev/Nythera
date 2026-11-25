#include "config/config_merge.h"
#include "config/config_service.h"
#include "config/schema/ranges.h"
#include "config/widget_config.h"
#include "core/files/resource_paths.h"
#include "core/input/key_chord.h"
#include "core/log.h"
#include "scripting/plugin_id.h"
#include "shell/settings/widget_settings_registry.h"
#include "theme/builtin_palettes.h"
#include "theme/custom_palettes.h"
#include "theme/scheme.h"
#include "util/file_utils.h"
#include "util/string_utils.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
  constexpr Logger kLog("config");

  void insertWidgetSetting(toml::table& table, const std::string& key, const WidgetSettingValue& value) {
    std::visit(
        [&](const auto& concrete) {
          using T = std::decay_t<decltype(concrete)>;
          if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            toml::array array;
            for (const auto& item : concrete) {
              array.push_back(item);
            }
            table.insert_or_assign(key, std::move(array));
          } else if constexpr (std::is_same_v<T, WidgetSettingStringMap>) {
            toml::table mapTable;
            std::vector<std::string> mapKeys;
            mapKeys.reserve(concrete.size());
            for (const auto& [mapKey, mapValue] : concrete) {
              (void)mapValue;
              mapKeys.push_back(mapKey);
            }
            std::ranges::sort(mapKeys);
            for (const auto& mapKey : mapKeys) {
              mapTable.insert_or_assign(mapKey, concrete.at(mapKey));
            }
            table.insert_or_assign(key, std::move(mapTable));
          } else {
            table.insert_or_assign(key, concrete);
          }
        },
        value
    );
  }

  toml::table desktopWidgetTable(const DesktopWidgetState& widget) {
    toml::table widgetTable;
    widgetTable.insert_or_assign("type", widget.type);
    widgetTable.insert_or_assign("output", widget.outputName);
    widgetTable.insert_or_assign("cx", static_cast<double>(widget.cx));
    widgetTable.insert_or_assign("cy", static_cast<double>(widget.cy));
    widgetTable.insert_or_assign("placement_width", static_cast<double>(widget.placementWidth));
    widgetTable.insert_or_assign("placement_height", static_cast<double>(widget.placementHeight));
    widgetTable.insert_or_assign("box_width", static_cast<double>(widget.boxWidth));
    widgetTable.insert_or_assign("box_height", static_cast<double>(widget.boxHeight));
    widgetTable.insert_or_assign("rotation", static_cast<double>(widget.rotationRad));
    if (widget.flipX) {
      widgetTable.insert_or_assign("flip_x", true);
    }
    if (widget.flipY) {
      widgetTable.insert_or_assign("flip_y", true);
    }
    if (!widget.enabled) {
      widgetTable.insert_or_assign("enabled", false);
    }
    if (!widget.settings.empty()) {
      toml::table settingsTable;
      for (const auto& [key, value] : widget.settings) {
        insertWidgetSetting(settingsTable, key, value);
      }
      widgetTable.insert_or_assign("settings", std::move(settingsTable));
    }
    return widgetTable;
  }

  void insertOverrideValue(toml::table& table, std::string_view key, const ConfigOverrideValue& value) {
    std::visit(
        [&](const auto& concrete) {
          using T = std::decay_t<decltype(concrete)>;
          if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            toml::array array;
            for (const auto& item : concrete) {
              array.push_back(item);
            }
            table.insert_or_assign(key, std::move(array));
          } else if constexpr (std::is_same_v<T, std::vector<ShortcutConfig>>) {
            toml::array array;
            for (const auto& item : concrete) {
              if (item.type.empty()) {
                continue;
              }
              toml::table shortcut;
              shortcut.insert_or_assign("type", item.type);
              array.push_back(std::move(shortcut));
            }
            table.insert_or_assign(key, std::move(array));
          } else if constexpr (std::is_same_v<T, std::vector<SessionPanelActionConfig>>) {
            toml::array array;
            for (const auto& item : concrete) {
              if (item.action.empty()) {
                continue;
              }
              toml::table row;
              row.insert_or_assign("action", item.action);
              row.insert_or_assign("enabled", item.enabled);
              if (item.command.has_value() && !item.command->empty()) {
                row.insert_or_assign("command", *item.command);
              }
              if (item.label.has_value() && !item.label->empty()) {
                row.insert_or_assign("label", *item.label);
              }
              if (item.glyph.has_value() && !item.glyph->empty()) {
                row.insert_or_assign("glyph", *item.glyph);
              }
              row.insert_or_assign("variant", std::string(enumToKey(kSessionActionButtonVariants, item.variant)));
              if (item.shortcut.has_value()) {
                row.insert_or_assign("shortcut", keyChordToString(*item.shortcut));
              }
              if (item.lockscreenShortcut.has_value()) {
                row.insert_or_assign("lockscreen_shortcut", keyChordToString(*item.lockscreenShortcut));
              }
              row.insert_or_assign("countdown_seconds", item.countdownSeconds);
              array.push_back(std::move(row));
            }
            table.insert_or_assign(key, std::move(array));
          } else if constexpr (std::is_same_v<T, std::vector<BarCapsuleGroupStyle>>) {
            toml::array array;
            for (const auto& item : concrete) {
              if (item.id.empty()) {
                continue;
              }
              toml::table row;
              row.insert_or_assign("id", item.id);
              row.insert_or_assign("enabled", item.enabled);
              toml::array members;
              for (const auto& member : item.members) {
                members.push_back(member);
              }
              row.insert_or_assign("members", std::move(members));
              row.insert_or_assign("fill", colorSpecToConfigString(item.fill));
              if (item.borderSpecified) {
                row.insert_or_assign(
                    "border", item.border.has_value() ? colorSpecToConfigString(*item.border) : std::string{}
                );
              }
              row.insert_or_assign("border_width", static_cast<double>(item.borderWidth));
              if (item.foreground.has_value()) {
                row.insert_or_assign("foreground", colorSpecToConfigString(*item.foreground));
              }
              row.insert_or_assign("padding", static_cast<double>(item.padding));
              if (item.radius.has_value()) {
                row.insert_or_assign("radius", static_cast<double>(*item.radius));
              }
              row.insert_or_assign("opacity", static_cast<double>(item.opacity));
              row.insert_or_assign("accordion", item.accordion);
              row.insert_or_assign(
                  "accordion_direction", std::string(enumToKey(kBarAccordionDirections, item.accordionDirection))
              );
              if (item.accordionDurationMs.has_value()) {
                row.insert_or_assign("accordion_duration", static_cast<std::int64_t>(*item.accordionDurationMs));
              }
              if (item.accordionDelayMs.has_value()) {
                row.insert_or_assign("accordion_delay_ms", static_cast<std::int64_t>(*item.accordionDelayMs));
              }
              if (item.widgetSpacing.has_value()) {
                row.insert_or_assign("widget_spacing", static_cast<std::int64_t>(*item.widgetSpacing));
              }
              array.push_back(std::move(row));
            }
            table.insert_or_assign(key, std::move(array));
          } else if constexpr (std::is_same_v<T, std::vector<IdleBehaviorConfig>>) {
            toml::table behaviorTable;
            toml::array behaviorOrder;
            for (const auto& item : concrete) {
              if (item.name.empty()) {
                continue;
              }
              toml::table row;
              row.insert_or_assign("enabled", item.enabled);
              row.insert_or_assign("timeout", item.timeoutSeconds);
              if (item.lockedTimeoutSeconds > 0.0) {
                row.insert_or_assign("locked_timeout", item.lockedTimeoutSeconds);
              }
              if (!item.action.empty()) {
                row.insert_or_assign("action", item.action);
              }
              if (!item.command.empty()) {
                row.insert_or_assign("command", item.command);
              }
              if (!item.resumeCommand.empty()) {
                row.insert_or_assign("resume_command", item.resumeCommand);
              }
              if (item.action == "suspend" && !item.lockBeforeSuspend) {
                row.insert_or_assign("lock_before_suspend", false);
              }
              behaviorTable.insert_or_assign(item.name, std::move(row));
              behaviorOrder.push_back(item.name);
            }
            table.insert_or_assign(key, std::move(behaviorTable));
            // Preserve user-defined behavior list order (table iteration order is not
            // a reliable ordering source after round-trips).
            if (key == "behavior") {
              table.insert_or_assign("behavior_order", std::move(behaviorOrder));
            }
          } else if constexpr (std::is_same_v<T, std::vector<NotificationFilterConfig>>) {
            toml::table filterTable;
            toml::array filterOrder;
            for (const auto& item : concrete) {
              if (item.name.empty()) {
                continue;
              }
              toml::table row;
              row.insert_or_assign("enabled", item.enabled);
              if (!item.match.empty()) {
                row.insert_or_assign("match", item.match);
              }
              if (!item.matchContent.empty()) {
                row.insert_or_assign("match_content", item.matchContent);
              }
              row.insert_or_assign("show_toast", item.showToast);
              row.insert_or_assign("save_history", item.saveHistory);
              row.insert_or_assign("play_sound", item.playSound);
              row.insert_or_assign("bypass_dnd", item.bypassDnd);
              row.insert_or_assign("allow_permanent", item.allowPermanent);
              if (item.overrideDuration.has_value()) {
                row.insert_or_assign("override_duration", static_cast<std::int64_t>(*item.overrideDuration));
              }
              if (!item.allowedUrgencies.empty()) {
                toml::array urgencies;
                for (const auto& urgency : item.allowedUrgencies) {
                  urgencies.push_back(urgency);
                }
                row.insert_or_assign("allowed_urgencies", std::move(urgencies));
              }
              filterTable.insert_or_assign(item.name, std::move(row));
              filterOrder.push_back(item.name);
            }
            table.insert_or_assign(key, std::move(filterTable));
            if (key == "filter") {
              table.insert_or_assign("filter_order", std::move(filterOrder));
            }
          } else if constexpr (std::is_same_v<T, std::vector<KeyChord>>) {
            toml::array array;
            for (const auto& item : concrete) {
              std::string serialized = keyChordToString(item);
              if (serialized.empty()) {
                continue;
              }
              array.push_back(std::move(serialized));
            }
            table.insert_or_assign(key, std::move(array));
          } else {
            table.insert_or_assign(key, concrete);
          }
        },
        value
    );
  }

  toml::table* ensureTable(toml::table& parent, std::string_view key) {
    if (auto* existing = parent.get_as<toml::table>(key)) {
      return existing;
    }
    auto [it, _] = parent.insert_or_assign(key, toml::table{});
    return it->second.as_table();
  }

  std::string joinDottedPath(const std::vector<std::string>& path) {
    std::string key;
    for (const auto& part : path) {
      if (!key.empty()) {
        key.push_back('.');
      }
      key += part;
    }
    return key;
  }

  const toml::node* findNode(const toml::table& root, const std::vector<std::string>& path) {
    const toml::table* table = &root;
    for (std::size_t i = 0; i < path.size(); ++i) {
      if (i + 1 == path.size()) {
        return table->get(path[i]);
      }
      const auto* next = table->get_as<toml::table>(path[i]);
      if (next == nullptr) {
        return nullptr;
      }
      table = next;
    }
    return nullptr;
  }

  void pruneEmptyTables(toml::table& root, const std::vector<std::string>& changedPath, std::size_t preserveDepth = 0) {
    if (changedPath.size() < 2) {
      return;
    }

    for (std::size_t depth = changedPath.size() - 1; depth > 0; --depth) {
      if (preserveDepth > 0 && depth <= preserveDepth) {
        break;
      }

      toml::table* parent = &root;
      for (std::size_t i = 0; i + 1 < depth; ++i) {
        parent = parent->get_as<toml::table>(changedPath[i]);
        if (parent == nullptr) {
          return;
        }
      }

      auto* node = parent->get(changedPath[depth - 1]);
      auto* table = node != nullptr ? node->as_table() : nullptr;
      if (table == nullptr || !table->empty()) {
        break;
      }
      parent->erase(changedPath[depth - 1]);
    }
  }

  bool erasePathFromTable(toml::table& root, const std::vector<std::string>& path, std::size_t preserveDepth = 0) {
    if (path.empty()) {
      return false;
    }

    toml::table* table = &root;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
      auto* next = table->get_as<toml::table>(path[i]);
      if (next == nullptr) {
        return false;
      }
      table = next;
    }

    if (table->erase(path.back()) == 0) {
      return false;
    }
    pruneEmptyTables(root, path, preserveDepth);
    return true;
  }

  std::vector<std::string> barOrderNames(const std::vector<BarConfig>& bars) {
    std::vector<std::string> order;
    order.reserve(bars.size());
    for (const auto& bar : bars) {
      order.push_back(bar.name);
    }
    return order;
  }

  const BarConfig* findBarConfig(const Config& cfg, std::string_view name) {
    const auto it = std::ranges::find(cfg.bars, name, &BarConfig::name);
    return it != cfg.bars.end() ? &*it : nullptr;
  }

  const BarMonitorOverride* findBarMonitorOverride(const BarConfig& bar, std::string_view match) {
    const auto it = std::ranges::find(bar.monitorOverrides, match, &BarMonitorOverride::match);
    return it != bar.monitorOverrides.end() ? &*it : nullptr;
  }

  bool isBarLanePath(const std::vector<std::string>& path) {
    const bool barScope = path.size() == 3 && path[0] == "bar";
    const bool monitorScope = path.size() == 5 && path[0] == "bar" && path[2] == "monitor";
    if (!barScope && !monitorScope) {
      return false;
    }
    const std::string& lane = path.back();
    return lane == "start" || lane == "center" || lane == "end";
  }

  std::vector<std::string> capsuleGroupPathForBarLanePath(const std::vector<std::string>& lanePath) {
    std::vector<std::string> path(lanePath.begin(), lanePath.end() - 1);
    path.emplace_back("capsule_group");
    return path;
  }

  const std::vector<std::string>* barLaneWidgets(const Config& cfg, const std::vector<std::string>& lanePath) {
    const BarConfig* bar = findBarConfig(cfg, lanePath[1]);
    if (bar == nullptr) {
      return nullptr;
    }
    const std::string& lane = lanePath.back();
    if (lanePath.size() == 5) {
      const BarMonitorOverride* ovr = findBarMonitorOverride(*bar, lanePath[3]);
      if (ovr == nullptr) {
        return nullptr;
      }
      const std::optional<std::vector<std::string>>& laneOverride =
          lane == "start" ? ovr->startWidgets : (lane == "center" ? ovr->centerWidgets : ovr->endWidgets);
      if (laneOverride.has_value()) {
        return &*laneOverride;
      }
    }
    return lane == "start" ? &bar->startWidgets : (lane == "center" ? &bar->centerWidgets : &bar->endWidgets);
  }

  const std::vector<BarCapsuleGroupStyle>*
  barLaneCapsuleGroups(const Config& cfg, const std::vector<std::string>& lanePath) {
    const BarConfig* bar = findBarConfig(cfg, lanePath[1]);
    if (bar == nullptr) {
      return nullptr;
    }
    if (lanePath.size() == 5) {
      const BarMonitorOverride* ovr = findBarMonitorOverride(*bar, lanePath[3]);
      if (ovr != nullptr && ovr->widgetCapsuleGroups.has_value()) {
        return &*ovr->widgetCapsuleGroups;
      }
    }
    return &bar->widgetCapsuleGroups;
  }

  void collectLaneGroupIds(const std::vector<std::string>& lane, std::set<std::string>& out) {
    for (const std::string& entry : lane) {
      if (isCapsuleGroupToken(entry)) {
        out.insert(capsuleGroupTokenId(entry));
      }
    }
  }

  bool overridePresenceIsSemantic(const std::vector<std::string>& path) {
    if (path.size() == 3 && path[0] == "widget" && path[2] == "type") {
      return true;
    }

    // migrateCustomSchedule() turns custom_schedule on for a times-only location whenever the key is
    // absent. Writing an explicit true value opts the user's config out of that legacy warning.
    if (path.size() == 2 && path[0] == "location" && path[1] == "custom_schedule") {
      return true;
    }

    if (path.size() == 5 && path[0] == "bar" && path[2] == "monitor") {
      const auto& key = path[4];
      return key == "start" || key == "center" || key == "end";
    }
    return false;
  }

  toml::table serializeWidgetsPlacementSection(
      const DesktopWidgetsGridState& grid, const std::vector<DesktopWidgetState>& widgets, std::optional<bool> enabled,
      std::int32_t schemaVersion
  ) {
    toml::table sectionTbl;
    if (enabled.has_value()) {
      sectionTbl.insert_or_assign("enabled", *enabled);
    }
    sectionTbl.insert_or_assign("schema_version", static_cast<std::int64_t>(schemaVersion));

    toml::table gridTable;
    gridTable.insert_or_assign("visible", grid.visible);
    gridTable.insert_or_assign("cell_size", static_cast<std::int64_t>(grid.cellSize));
    gridTable.insert_or_assign("major_interval", static_cast<std::int64_t>(grid.majorInterval));
    sectionTbl.insert_or_assign("grid", std::move(gridTable));

    toml::table widgetTable;
    toml::array widgetOrder;
    for (const auto& widget : widgets) {
      if (widget.id.empty() || widget.type.empty()) {
        continue;
      }
      widgetTable.insert_or_assign(widget.id, desktopWidgetTable(widget));
      widgetOrder.push_back(widget.id);
    }
    sectionTbl.insert_or_assign("widget", std::move(widgetTable));
    sectionTbl.insert_or_assign("widget_order", std::move(widgetOrder));
    return sectionTbl;
  }

  toml::array serializeWallpaperFavorites(const std::vector<WallpaperFavorite>& favorites) {
    toml::array favoritesArray;
    favoritesArray.reserve(favorites.size());
    for (const auto& favorite : favorites) {
      toml::table entry;
      entry.insert("path", favorite.path);
      if (favorite.themeMode.has_value()) {
        entry.insert("theme_mode", std::string(enumToKey(kThemeModes, *favorite.themeMode)));
      }
      if (favorite.paletteSource.has_value()) {
        entry.insert("palette_source", std::string(enumToKey(kPaletteSources, *favorite.paletteSource)));
        switch (*favorite.paletteSource) {
        case PaletteSource::Builtin:
          if (!favorite.builtinPalette.empty()) {
            entry.insert("builtin_palette", favorite.builtinPalette);
          }
          break;
        case PaletteSource::Wallpaper:
          if (!favorite.wallpaperScheme.empty()) {
            entry.insert("wallpaper_scheme", favorite.wallpaperScheme);
          }
          break;
        case PaletteSource::Community:
          if (!favorite.communityPalette.empty()) {
            entry.insert("community_palette", favorite.communityPalette);
          }
          break;
        case PaletteSource::Custom:
          if (!favorite.customPalette.empty()) {
            entry.insert("custom_palette", favorite.customPalette);
          }
          break;
        }
      }
      favoritesArray.push_back(std::move(entry));
    }
    return favoritesArray;
  }

  std::string formatStringValue(std::string_view text) {
    toml::value<std::string> v{std::string(text)};
    // Delegate to ConfigService::formatTomlValue via a temporary node.
    toml::table t;
    t.insert("v", v.get());
    return ConfigService::formatTomlValue(*t.get("v"));
  }

  std::string formatStringArrayValue(const std::vector<std::string>& items) {
    toml::array arr;
    for (const auto& item : items) {
      arr.push_back(item);
    }
    return ConfigService::formatTomlValue(arr);
  }

  template <typename Edit>
  void syncTableEdits(
      const std::vector<std::string>& prefix, const toml::node* currentNode, const toml::table& desired,
      std::vector<Edit>& out, const std::optional<std::filesystem::path>& destination = std::nullopt
  ) {
    const toml::table* currentTbl = currentNode != nullptr ? currentNode->as_table() : nullptr;
    if (currentTbl != nullptr) {
      for (const auto& [k, _] : *currentTbl) {
        const std::string key(k.str());
        if (!desired.contains(key)) {
          auto childPath = prefix;
          childPath.push_back(key);
          out.push_back({std::move(childPath), std::nullopt, destination});
        }
      }
    }
    if (desired.empty() && currentTbl == nullptr) {
      out.push_back({prefix, "{}", destination});
      return;
    }
    for (const auto& [k, v] : desired) {
      auto childPath = prefix;
      childPath.emplace_back(k.str());
      const toml::node* existingChild = currentTbl != nullptr ? currentTbl->get(k) : nullptr;
      if (const auto* subTbl = v.as_table()) {
        if (existingChild != nullptr && !existingChild->is_table()) {
          out.push_back({childPath, std::nullopt, destination});
        }
        syncTableEdits(childPath, existingChild, *subTbl, out, destination);
      } else {
        out.push_back({std::move(childPath), ConfigService::formatTomlValue(v), destination});
      }
    }
  }

  template <typename Edit>
  void emitSubtreeEdits(
      const std::vector<std::string>& prefix, const toml::table& tbl, std::vector<Edit>& out,
      const std::optional<std::filesystem::path>& destination
  ) {
    if (tbl.empty()) {
      out.push_back({prefix, "{}", destination});
      return;
    }
    for (const auto& [k, v] : tbl) {
      auto childPath = prefix;
      childPath.emplace_back(k.str());
      if (const auto* subTbl = v.as_table()) {
        emitSubtreeEdits(childPath, *subTbl, out, destination);
      } else {
        out.push_back({std::move(childPath), ConfigService::formatTomlValue(v), destination});
      }
    }
  }

  template <typename Edit>
  void appendOverrideValueEdits(
      const std::vector<std::string>& path, const ConfigOverrideValue& value, const toml::table& currentUserTable,
      std::vector<Edit>& out
  ) {
    toml::table temp;
    insertOverrideValue(temp, path.back(), value);

    if (const auto* node = temp.get(path.back()); node != nullptr && node->is_table()) {
      syncTableEdits(path, findNode(currentUserTable, path), *node->as_table(), out);
      if (path.size() == 2 && path[0] == "idle" && path[1] == "behavior") {
        if (const auto* orderNode = temp.get("behavior_order")) {
          out.push_back({{"idle", "behavior_order"}, ConfigService::formatTomlValue(*orderNode), std::nullopt});
        }
      } else if (path.size() == 2 && path[0] == "notification" && path[1] == "filter") {
        if (const auto* orderNode = temp.get("filter_order")) {
          out.push_back({{"notification", "filter_order"}, ConfigService::formatTomlValue(*orderNode), std::nullopt});
        }
      }
      return;
    }

    if (const auto* node = temp.get(path.back())) {
      out.push_back({path, ConfigService::formatTomlValue(*node), std::nullopt});
    }
  }

} // namespace

void ConfigService::setPluginEnabled(std::string_view pluginId, bool enabled) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty() || !scripting::isValidPluginId(pluginId)) {
    return;
  }

  const std::string id(pluginId);
  std::vector<std::string> next = m_config.plugins.enabled;
  const bool currentlyEnabled = std::ranges::contains(next, id);

  if (enabled) {
    if (currentlyEnabled) {
      return;
    }
    next.push_back(id);
  } else {
    if (!currentlyEnabled) {
      return;
    }
    std::erase(next, id);
  }

  const std::vector<DirectEdit> edits{{{"plugins", "enabled"}, formatStringArrayValue(next), std::nullopt}};
  (void)persistDirectEdits(edits);
}

void ConfigService::addPluginSource(const PluginSourceConfig& source) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty() || !isValidPluginSourceName(source.name)) {
    return;
  }

  const auto sourceTable = [](const PluginSourceConfig& src) {
    toml::table entry;
    entry.insert_or_assign("name", src.name);
    entry.insert_or_assign("kind", std::string(enumToKey(kPluginSourceKinds, src.kind)));
    entry.insert_or_assign("location", src.location);
    if (!src.enabled) {
      entry.insert_or_assign("enabled", false);
    }
    return entry;
  };

  toml::array nextSources;
  bool sourceWritten = false;
  if (const auto* existingNode = findNode(m_mergedUserTable, {"plugins", "source"});
      existingNode != nullptr && existingNode->is_array()) {
    nextSources = *existingNode->as_array();
    for (auto it = nextSources.begin(); it != nextSources.end(); ++it) {
      const auto* tbl = it->as_table();
      const auto name = tbl != nullptr ? (*tbl)["name"].value<std::string>() : std::nullopt;
      if (name && *name == source.name) {
        nextSources.replace(it, sourceTable(source));
        sourceWritten = true;
        break;
      }
    }
    if (!sourceWritten) {
      nextSources.push_back(sourceTable(source));
    }
  } else {
    for (const auto& existing : m_config.plugins.sources) {
      if (!isValidPluginSourceName(existing.name)) {
        continue;
      }
      nextSources.push_back(sourceTable(existing.name == source.name ? source : existing));
      sourceWritten = sourceWritten || existing.name == source.name;
    }
    if (!sourceWritten) {
      nextSources.push_back(sourceTable(source));
    }
  }

  const std::vector<DirectEdit> edits{{{"plugins", "source"}, formatTomlValue(nextSources), std::nullopt}};
  (void)persistDirectEdits(edits);
}

void ConfigService::removePluginSource(std::string_view name) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }
  const auto* existingNode = findNode(m_mergedUserTable, {"plugins", "source"});
  if (existingNode == nullptr || !existingNode->is_array()) {
    return;
  }

  toml::array nextSources = *existingNode->as_array();
  bool removed = false;
  for (auto it = nextSources.begin(); it != nextSources.end();) {
    const auto* tbl = it->as_table();
    const auto entryName = tbl != nullptr ? (*tbl)["name"].value<std::string>() : std::nullopt;
    if (entryName && *entryName == name) {
      it = nextSources.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (!removed) {
    return;
  }

  const std::vector<DirectEdit> edits{{{"plugins", "source"}, formatTomlValue(nextSources), std::nullopt}};
  (void)persistDirectEdits(edits);
}

void ConfigService::setThemeMode(ThemeMode mode) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }
  const std::vector<DirectEdit> edits{
      {{"theme", "mode"}, formatStringValue(enumToKey(kThemeModes, mode)), std::nullopt}
  };
  (void)persistDirectEdits(edits);
}

bool ConfigService::setThemeColorScheme(PaletteSource source, std::string_view valueRaw) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return false;
  }

  const std::string value = StringUtils::trim(valueRaw);
  if (value.empty()) {
    return false;
  }

  std::string paletteKey;
  switch (source) {
  case PaletteSource::Builtin:
    if (nythera::theme::findBuiltinPalette(value) == nullptr) {
      return false;
    }
    paletteKey = "builtin";
    break;
  case PaletteSource::Wallpaper:
    if (!nythera::theme::schemeFromString(value)) {
      return false;
    }
    paletteKey = "wallpaper_scheme";
    break;
  case PaletteSource::Community:
    paletteKey = "community_palette";
    break;
  case PaletteSource::Custom:
    if (!std::filesystem::exists(nythera::theme::customPalettePath(value))) {
      return false;
    }
    paletteKey = "custom_palette";
    break;
  }

  const std::vector<DirectEdit> edits{
      {{"theme", "source"}, formatStringValue(enumToKey(kPaletteSources, source)), std::nullopt},
      {{"theme", std::move(paletteKey)}, formatStringValue(value), std::nullopt},
  };
  return persistDirectEdits(edits);
}

void ConfigService::setDockEnabled(bool enabled) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }
  if (const auto* node = findNode(m_mergedUserTable, {"dock", "enabled"})) {
    const auto existing = node->value<bool>();
    if (existing.has_value() && *existing == enabled && m_config.dock.enabled == enabled) {
      return;
    }
  }

  const std::vector<DirectEdit> edits{{{"dock", "enabled"}, enabled ? "true" : "false", std::nullopt}};
  (void)persistDirectEdits(edits);
}

void ConfigService::setPluginsAutoUpdate(PluginAutoUpdateMode mode) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }
  if (const auto* node = findNode(m_mergedUserTable, {"plugins", "auto_update"})) {
    const auto existingKey = node->value<std::string>();
    const auto existingMode =
        existingKey.has_value() ? enumFromKey(kPluginAutoUpdateModes, *existingKey) : std::nullopt;
    if (existingMode.has_value() && *existingMode == mode && m_config.plugins.autoUpdate == mode) {
      return;
    }
  }

  const std::vector<DirectEdit> edits{
      {{"plugins", "auto_update"}, formatStringValue(enumToKey(kPluginAutoUpdateModes, mode)), std::nullopt}
  };
  (void)persistDirectEdits(edits);
}

bool ConfigService::setDesktopWidgetsState(const DesktopWidgetsConfig& desktopWidgets) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return false;
  }
  const toml::table desired = serializeWidgetsPlacementSection(
      desktopWidgets.grid, desktopWidgets.widgets, std::nullopt, desktopWidgets.schemaVersion
  );
  std::vector<DirectEdit> edits;
  syncTableEdits({"desktop_widgets"}, findNode(m_mergedUserTable, {"desktop_widgets"}), desired, edits);
  return persistDirectEdits(edits);
}

bool ConfigService::setLockscreenWidgetsState(const LockscreenWidgetsConfig& lockscreenWidgets) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return false;
  }
  const toml::table desired = serializeWidgetsPlacementSection(
      lockscreenWidgets.grid, lockscreenWidgets.widgets, lockscreenWidgets.enabled, lockscreenWidgets.schemaVersion
  );
  std::vector<DirectEdit> edits;
  syncTableEdits({"lockscreen_widgets"}, findNode(m_mergedUserTable, {"lockscreen_widgets"}), desired, edits);
  return persistDirectEdits(edits);
}

bool ConfigService::markSetupWizardCompleted() {
  if (m_setupMarkerPath.empty()) {
    return false;
  }
  if (std::filesystem::exists(m_setupMarkerPath)) {
    return true;
  }

  // The bundled wallpaper is served via firstRunWallpaperPath() until setup
  // completes. Persist it so finishing the wizard does not clear the desktop.
  if (!hasConfiguredWallpaper()) {
    const auto bundled = paths::assetPath("nythera-wallpaper.jpeg");
    std::error_code ec;
    if (std::filesystem::exists(bundled, ec)) {
      setWallpaperPath(std::nullopt, bundled.string());
    }
  }

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(m_setupMarkerPath).parent_path(), ec);
  std::ofstream out(m_setupMarkerPath, std::ios::trunc);
  if (!out.is_open()) {
    kLog.warn("failed to write {}", m_setupMarkerPath);
    return false;
  }
  return true;
}

bool ConfigService::isOverrideOnlyCalendarAccount(std::string_view id) const {
  if (id.empty()) {
    return false;
  }
  const auto* node = findNode(m_mergedUserTable, {"calendar", "account", std::string(id)});
  return node != nullptr && node->is_table();
}

bool ConfigService::canMoveBarOverride(std::string_view name, int direction) const {
  if (direction == 0 || name.empty()) {
    return false;
  }

  const auto barIt = std::ranges::find(m_config.bars, name, &BarConfig::name);
  if (barIt == m_config.bars.end()) {
    return false;
  }

  if (direction < 0) {
    return barIt != m_config.bars.begin();
  }

  return std::next(barIt) != m_config.bars.end();
}

bool ConfigService::canDeleteBarOverride(std::string_view name) const {
  return m_config.bars.size() > 1 && isOverrideOnlyBar(name);
}

bool ConfigService::isOverrideOnlyDockMonitorOverride(std::string_view tableName) const {
  if (tableName.empty()) {
    return false;
  }
  const auto* node = findNode(m_mergedUserTable, {"dock", "monitor", std::string(tableName)});
  return node != nullptr && node->is_table();
}

bool ConfigService::createBarOverride(std::string_view name) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty() || name.empty()) {
    return false;
  }

  for (const auto& bar : m_config.bars) {
    if (bar.name == name) {
      return false;
    }
  }
  if (findNode(m_mergedUserTable, {"bar", std::string(name)}) != nullptr) {
    return false;
  }

  std::vector<DirectEdit> edits;
  const auto* barMap = m_mergedUserTable["bar"].as_table();
  const bool hasAnyBarTable =
      barMap != nullptr && std::ranges::any_of(*barMap, [](const auto& entry) { return entry.second.is_table(); });
  if (!hasAnyBarTable && m_config.bars.size() == 1 && m_config.bars.front().name == "default") {
    edits.push_back({{"bar", "default", "enabled"}, m_config.bars.front().enabled ? "true" : "false", std::nullopt});
  }

  edits.push_back({{"bar", std::string(name), "enabled"}, "true", std::nullopt});
  auto order = barOrderNames(m_config.bars);
  order.emplace_back(name);
  edits.push_back({{"bar", "order"}, formatStringArrayValue(order), std::nullopt});

  return persistDirectEdits(edits);
}

bool ConfigService::moveBarOverride(std::string_view name, int direction) {
  if (!canMoveBarOverride(name, direction)) {
    return false;
  }

  auto order = barOrderNames(m_config.bars);
  const auto currentIt = std::ranges::find(order, name);
  if (currentIt == order.end()) {
    return false;
  }

  if (direction < 0) {
    std::iter_swap(currentIt, std::prev(currentIt));
  } else {
    std::iter_swap(currentIt, std::next(currentIt));
  }

  const std::vector<DirectEdit> edits{{{"bar", "order"}, formatStringArrayValue(order), std::nullopt}};
  return persistDirectEdits(edits);
}

bool ConfigService::renameBarOverride(std::string_view oldName, std::string_view newName) {
  if (oldName.empty() || newName.empty() || oldName == newName || !isOverrideOnlyBar(oldName)) {
    return false;
  }

  for (const auto& bar : m_config.bars) {
    if (bar.name == newName) {
      return false;
    }
  }
  if (findNode(m_mergedUserTable, {"bar", std::string(newName)}) != nullptr) {
    return false;
  }

  const std::vector<std::string> oldPath{"bar", std::string(oldName)};
  const std::vector<std::string> newPath{"bar", std::string(newName)};
  const auto* oldNode = findNode(m_mergedUserTable, oldPath);
  if (oldNode == nullptr || !oldNode->is_table()) {
    return false;
  }

  std::optional<std::filesystem::path> dest;
  if (const auto* winner = m_sourceMap.winner(oldPath)) {
    dest = std::filesystem::path(winner->origin.file);
  } else {
    dest = defaultDestinationForPath(newPath);
  }

  std::vector<DirectEdit> edits;
  edits.push_back({oldPath, std::nullopt, std::nullopt});
  emitSubtreeEdits(newPath, *oldNode->as_table(), edits, dest);

  auto order = barOrderNames(m_config.bars);
  for (auto& item : order) {
    if (item == oldName) {
      item = std::string(newName);
      break;
    }
  }
  edits.push_back({{"bar", "order"}, formatStringArrayValue(order), std::nullopt});

  // shell.panel_anchor_bar names a bar; follow the rename so panels keep opening.
  if (m_config.shell.panelAnchorBar == oldName) {
    edits.push_back({{"shell", "panel_anchor_bar"}, formatStringValue(newName), std::nullopt});
  }

  return persistDirectEdits(edits);
}

bool ConfigService::deleteBarOverride(std::string_view name) {
  if (!canDeleteBarOverride(name)) {
    return false;
  }

  auto order = barOrderNames(m_config.bars);
  std::erase(order, std::string(name));

  std::vector<DirectEdit> edits;
  edits.push_back({{"bar", std::string(name)}, std::nullopt, std::nullopt});
  edits.push_back({{"bar", "order"}, formatStringArrayValue(order), std::nullopt});
  if (m_config.shell.panelAnchorBar == name) {
    edits.push_back({{"shell", "panel_anchor_bar"}, std::nullopt, std::nullopt});
  }
  return persistDirectEdits(edits);
}

bool ConfigService::createMonitorOverride(std::string_view barName, std::string_view match) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty() || barName.empty() || match.empty()) {
    return false;
  }

  const auto barIt = std::ranges::find(m_config.bars, barName, &BarConfig::name);
  if (barIt == m_config.bars.end()) {
    return false;
  }
  const auto monitorIt = std::find_if(
      barIt->monitorOverrides.begin(), barIt->monitorOverrides.end(),
      [match](const BarMonitorOverride& ovr) { return ovr.match == match; }
  );
  if (monitorIt != barIt->monitorOverrides.end()) {
    return false;
  }
  const std::vector<std::string> path{"bar", std::string(barName), "monitor", std::string(match)};
  if (findNode(m_mergedUserTable, path) != nullptr) {
    return false;
  }

  const std::vector<DirectEdit> edits{{path, "{}", std::nullopt}};
  return persistDirectEdits(edits);
}

bool ConfigService::renameMonitorOverride(
    std::string_view barName, std::string_view oldMatch, std::string_view newMatch
) {
  if (barName.empty()
      || oldMatch.empty()
      || newMatch.empty()
      || oldMatch == newMatch
      || !isOverrideOnlyMonitorOverride(barName, oldMatch)) {
    return false;
  }

  const auto barIt = std::ranges::find(m_config.bars, barName, &BarConfig::name);
  if (barIt == m_config.bars.end()) {
    return false;
  }
  const auto monitorIt = std::find_if(
      barIt->monitorOverrides.begin(), barIt->monitorOverrides.end(),
      [newMatch](const BarMonitorOverride& ovr) { return ovr.match == newMatch; }
  );
  if (monitorIt != barIt->monitorOverrides.end()) {
    return false;
  }

  return renameOverrideTable(
      {"bar", std::string(barName), "monitor", std::string(oldMatch)},
      {"bar", std::string(barName), "monitor", std::string(newMatch)}
  );
}

bool ConfigService::deleteMonitorOverride(std::string_view barName, std::string_view match) {
  if (!isOverrideOnlyMonitorOverride(barName, match)) {
    return false;
  }
  return clearOverride({"bar", std::string(barName), "monitor", std::string(match)});
}

bool ConfigService::createDockMonitorOverride(std::string_view match) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty() || match.empty()) {
    return false;
  }
  if (std::ranges::any_of(m_config.dock.monitorOverrides, [match](const DockMonitorOverride& override) {
        return override.tableName == match || override.match == match;
      })) {
    return false;
  }
  const std::vector<std::string> path{"dock", "monitor", std::string(match)};
  if (findNode(m_mergedUserTable, path) != nullptr) {
    return false;
  }

  const std::vector<DirectEdit> edits{{path, "{}", std::nullopt}};
  return persistDirectEdits(edits);
}

bool ConfigService::renameDockMonitorOverride(std::string_view oldTableName, std::string_view newMatch) {
  if (oldTableName.empty()
      || newMatch.empty()
      || oldTableName == newMatch
      || !isOverrideOnlyDockMonitorOverride(oldTableName)) {
    return false;
  }
  if (std::ranges::any_of(
          m_config.dock.monitorOverrides, [oldTableName, newMatch](const DockMonitorOverride& override) {
            return override.tableName != oldTableName && (override.tableName == newMatch || override.match == newMatch);
          }
      )) {
    return false;
  }
  return renameOverrideTable(
      {"dock", "monitor", std::string(oldTableName)}, {"dock", "monitor", std::string(newMatch)}
  );
}

bool ConfigService::deleteDockMonitorOverride(std::string_view tableName) {
  if (!isOverrideOnlyDockMonitorOverride(tableName)) {
    return false;
  }
  return clearOverride({"dock", "monitor", std::string(tableName)});
}

bool ConfigService::deleteCalendarAccountOverride(std::string_view id) {
  if (!isOverrideOnlyCalendarAccount(id)) {
    return false;
  }
  return clearOverride({"calendar", "account", std::string(id)});
}

bool ConfigService::setOverride(const std::vector<std::string>& path, ConfigOverrideValue value) {
  return setOverride(path, std::move(value), nullptr);
}

bool ConfigService::validateOverride(
    const std::vector<std::string>& path, const ConfigOverrideValue& value, std::string* error
) {
  m_lastMutationError.clear();
  if (path.empty()) {
    if (error != nullptr) {
      *error = "invalid empty setting path";
    }
    return false;
  }

  std::vector<std::filesystem::path> baseFiles;
  if (!m_sharedThemePath.empty()) {
    baseFiles.emplace_back(m_sharedThemePath);
  }
  auto current = nythera::config::mergeConfigWithSources(m_configDir, {}, baseFiles);
  if (!current.config.firstError.empty()) {
    m_lastMutationError = current.config.firstErrorOrigin.prefixedShort(m_configDir, current.config.firstError);
    if (error != nullptr) {
      *error = m_lastMutationError;
    }
    return false;
  }

  toml::table baselineMerged = current.config.merged;
  nythera::config::ConfigOriginIndex baselineOrigins = current.config.origins;
  toml::table baselineSidecar;
  bool hasBaselineSidecar = false;
  ConfigProblem legacyError;
  if (!overlayLegacySidecar(baselineMerged, baselineOrigins, baselineSidecar, hasBaselineSidecar, &legacyError)) {
    m_lastMutationError = legacyError.flatten(m_configDir);
    if (error != nullptr) {
      *error = m_lastMutationError;
    }
    return false;
  }
  const auto baselineDiagnostics =
      diagnosticsForTable(baselineMerged, baselineOrigins, hasBaselineSidecar ? &baselineSidecar : nullptr);

  toml::table candidate = baselineMerged;
  toml::table* table = &candidate;
  for (std::size_t i = 0; i + 1 < path.size(); ++i) {
    table = ensureTable(*table, path[i]);
    if (table == nullptr) {
      if (error != nullptr) {
        *error = "setting path conflicts with a non-table value";
      }
      return false;
    }
  }
  insertOverrideValue(*table, path.back(), value);

  const auto candidateDiagnostics =
      diagnosticsForTable(candidate, baselineOrigins, hasBaselineSidecar ? &baselineSidecar : nullptr);
  const std::string settingPath = joinDottedPath(path);
  const auto fieldError = std::ranges::find_if(candidateDiagnostics.entries, [&](const auto& entry) {
    return entry.severity == nythera::config::schema::Diagnostics::Severity::Error && entry.path == settingPath;
  });
  if (fieldError != candidateDiagnostics.entries.end()) {
    m_lastMutationError = fieldError->describeShort(m_configDir);
    if (error != nullptr) {
      *error = m_lastMutationError;
    }
    return false;
  }

  const auto fatal = std::ranges::find_if(candidateDiagnostics.entries, [](const auto& entry) {
    return entry.severity == nythera::config::schema::Diagnostics::Severity::Error
        && entry.recoveryScope == nythera::config::schema::Diagnostics::RecoveryScope::Document;
  });
  if (fatal != candidateDiagnostics.entries.end()) {
    m_lastMutationError = fatal->describeShort(m_configDir);
    if (error != nullptr) {
      *error = m_lastMutationError;
    }
    return false;
  }

  const auto introduced = candidateDiagnostics.introducedErrorsComparedTo(baselineDiagnostics);
  if (!introduced.entries.empty()) {
    m_lastMutationError = introduced.entries.front().describeShort(m_configDir);
    if (error != nullptr) {
      *error = m_lastMutationError;
    }
    return false;
  }

  return true;
}

bool ConfigService::setOverride(const std::vector<std::string>& path, ConfigOverrideValue value, bool* changed) {
  std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> overrides;
  overrides.emplace_back(path, std::move(value));
  return setOverrides(overrides, changed);
}

bool ConfigService::setOverrides(
    const std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& overrides
) {
  return setOverrides(overrides, nullptr);
}

bool ConfigService::setOverrides(
    const std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& overrides, bool* changed
) {
  if (changed != nullptr) {
    *changed = false;
  }
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty() || overrides.empty()) {
    return false;
  }
  return mutateOverrides(overrides, {}, changed);
}

bool ConfigService::mutateOverrides(
    const std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& overrides,
    const std::vector<std::vector<std::string>>& clearPaths, bool* changed
) {
  if (changed != nullptr) {
    *changed = false;
  }
  m_lastMutationError.clear();
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return false;
  }

  const auto erasePath = [](toml::table& table, const std::vector<std::string>& path) {
    const bool erased = erasePathFromTable(table, path, preserveDepthForPath(path, table));
    if (erased && path.size() == 2 && path[0] == "idle" && path[1] == "behavior") {
      erasePathFromTable(table, {"idle", "behavior_order"}, preserveDepthForPath(path, table));
    }
    if (erased && path.size() == 2 && path[0] == "notification" && path[1] == "filter") {
      erasePathFromTable(table, {"notification", "filter_order"}, preserveDepthForPath(path, table));
    }
    return erased;
  };

  std::vector<DirectEdit> edits;
  toml::table next = m_mergedUserTable;
  std::vector<std::vector<std::string>> clearedLanePaths;
  for (const auto& path : clearPaths) {
    if (path.empty()) {
      return false;
    }
    if (erasePath(next, path)) {
      edits.push_back({path, std::nullopt, std::nullopt});
      if (path.size() == 2 && path[0] == "idle" && path[1] == "behavior") {
        edits.push_back({{"idle", "behavior_order"}, std::nullopt, std::nullopt});
      }
      if (path.size() == 2 && path[0] == "notification" && path[1] == "filter") {
        edits.push_back({{"notification", "filter_order"}, std::nullopt, std::nullopt});
      }
      if (isBarLanePath(path)) {
        clearedLanePaths.push_back(path);
      }
    } else if (m_usingLegacyConfigDir && findNode(m_mergedUserTable, path) != nullptr) {
      edits.push_back({path, std::nullopt, std::nullopt});
    }
  }

  for (const auto& [path, value] : overrides) {
    if (path.empty()) {
      return false;
    }
    toml::table* table = &next;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
      table = ensureTable(*table, path[i]);
      if (table == nullptr) {
        return false;
      }
    }
    insertOverrideValue(*table, path.back(), value);
  }

  for (const auto& [path, value] : overrides) {
    // Keep already-authored settings so editing back to the default edits in place;
    // omit brand-new entries that do not change effective Config to keep config sparse.
    const bool alreadyPresent = hasOverride(path);
    if (!alreadyPresent && !overridePresenceIsSemantic(path) && !pathEffectiveInUserTable(path, next)) {
      erasePath(next, path);
      continue;
    }
    appendOverrideValueEdits(path, value, m_mergedUserTable, edits);
  }

  // Reconcile orphaned capsule_group entries if a bar lane was cleared without an
  // explicit capsule_group update in the same mutation.
  if (!clearedLanePaths.empty()) {
    const auto hasGroupMutation = [&](const std::vector<std::string>& groupPath) {
      return std::ranges::any_of(overrides, [&](const auto& entry) { return entry.first == groupPath; })
          || std::ranges::any_of(clearPaths, [&](const auto& entry) { return entry == groupPath; });
    };
    const auto parsedAfterClear = parseMergedConfig(next);
    if (parsedAfterClear.has_value()) {
      for (const auto& lanePath : clearedLanePaths) {
        const std::vector<std::string> groupPath = capsuleGroupPathForBarLanePath(lanePath);
        if (hasGroupMutation(groupPath) || findNode(next, groupPath) == nullptr) {
          continue;
        }
        const BarConfig* bar = findBarConfig(*parsedAfterClear, lanePath[1]);
        if (bar == nullptr) {
          continue;
        }
        const bool monitorScope = lanePath.size() == 5;
        const BarMonitorOverride* ovr = monitorScope ? findBarMonitorOverride(*bar, lanePath[3]) : nullptr;
        if (monitorScope && (ovr == nullptr || !ovr->widgetCapsuleGroups.has_value())) {
          continue;
        }
        const std::vector<BarCapsuleGroupStyle>& current =
            monitorScope ? *ovr->widgetCapsuleGroups : bar->widgetCapsuleGroups;
        const std::set<std::string> remainingReferenced =
            monitorScope ? capsuleGroupRefsForMonitorScope(*bar, *ovr) : capsuleGroupRefsForBarScope(*bar);
        std::vector<BarCapsuleGroupStyle> reconciled;
        for (const auto& group : current) {
          if (remainingReferenced.contains(group.id)) {
            reconciled.push_back(group);
          }
        }
        if (reconciled == current) {
          continue;
        }
        if (reconciled.empty()) {
          erasePath(next, groupPath);
          edits.push_back({groupPath, std::nullopt, std::nullopt});
        } else {
          appendOverrideValueEdits(groupPath, ConfigOverrideValue{reconciled}, m_mergedUserTable, edits);
        }
      }
    }
  }

  if (edits.empty()) {
    return true;
  }

  return persistDirectEdits(edits, changed);
}

bool ConfigService::clearOverride(const std::vector<std::string>& path) {
  bool changed = false;
  if (!clearOverrides({path}, &changed)) {
    return false;
  }
  return changed;
}

bool ConfigService::clearOverrides(const std::vector<std::vector<std::string>>& paths, bool* changed) {
  return mutateOverrides({}, paths, changed);
}

bool ConfigService::resetBarLaneOverride(const std::vector<std::string>& lanePath, bool* changed) {
  if (changed != nullptr) {
    *changed = false;
  }
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty() || !isBarLanePath(lanePath)) {
    return false;
  }

  std::vector<DirectEdit> edits;
  if (findNode(m_mergedUserTable, lanePath) != nullptr) {
    edits.push_back({lanePath, std::nullopt, std::nullopt});
  }

  const std::vector<std::string> groupPath = capsuleGroupPathForBarLanePath(lanePath);
  if (findNode(m_mergedUserTable, groupPath) != nullptr) {
    toml::table withoutLane = m_mergedUserTable;
    erasePathFromTable(withoutLane, lanePath, preserveDepthForPath(lanePath, withoutLane));
    withoutLane.erase(nythera::config::kConfigVersionKey);
    nythera::config::LegacyConfigIssues ignoredIssues;
    nythera::config::normalizeLegacyConfig(withoutLane, ignoredIssues);
    const auto baseline = parseMergedConfig(withoutLane);
    if (!baseline.has_value()) {
      return false;
    }

    const BarConfig* baselineBar = findBarConfig(*baseline, lanePath[1]);
    const std::vector<BarCapsuleGroupStyle>* currentGroups = barLaneCapsuleGroups(m_config, lanePath);
    const std::vector<std::string>* currentLane = barLaneWidgets(m_config, lanePath);
    if (baselineBar != nullptr && currentGroups != nullptr && currentLane != nullptr) {
      std::set<std::string> laneOwned;
      collectLaneGroupIds(*currentLane, laneOwned);

      const bool monitorScope = lanePath.size() == 5;
      const BarMonitorOverride* baselineOvr =
          monitorScope ? findBarMonitorOverride(*baselineBar, lanePath[3]) : nullptr;
      const std::set<std::string> remainingReferenced = monitorScope
          ? (baselineOvr != nullptr ? capsuleGroupRefsForMonitorScope(*baselineBar, *baselineOvr)
                                    : std::set<std::string>{})
          : capsuleGroupRefsForBarScope(*baselineBar);

      std::vector<BarCapsuleGroupStyle> remaining;
      remaining.reserve(currentGroups->size());
      for (const auto& group : *currentGroups) {
        if (laneOwned.contains(group.id) && !remainingReferenced.contains(group.id)) {
          continue;
        }
        remaining.push_back(group);
      }

      if (remaining != *currentGroups) {
        if (remaining.empty()) {
          edits.push_back({groupPath, std::nullopt, std::nullopt});
        } else {
          appendOverrideValueEdits(groupPath, ConfigOverrideValue{remaining}, m_mergedUserTable, edits);
        }
      }
    }
  }

  if (edits.empty()) {
    m_lastMutationError.clear();
    return true;
  }

  return persistDirectEdits(edits, changed);
}

bool ConfigService::renameOverrideTable(
    const std::vector<std::string>& oldPath, const std::vector<std::string>& newPath
) {
  return renameOverrideTable(oldPath, newPath, {});
}

bool ConfigService::renameOverrideTable(
    const std::vector<std::string>& oldPath, const std::vector<std::string>& newPath,
    const std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& extraOverrides
) {
  m_lastMutationError.clear();
  if (m_settingsAccess != SettingsAccess::ReadWrite
      || m_configDir.empty()
      || oldPath.empty()
      || newPath.empty()
      || oldPath == newPath) {
    return false;
  }

  const toml::node* oldNode = findNode(m_mergedUserTable, oldPath);
  if (oldNode == nullptr || !oldNode->is_table()) {
    return false;
  }
  if (findNode(m_mergedUserTable, newPath) != nullptr) {
    return false;
  }

  std::optional<std::filesystem::path> dest;
  if (const auto* winner = m_sourceMap.winner(oldPath)) {
    dest = std::filesystem::path(winner->origin.file);
  } else {
    dest = defaultDestinationForPath(newPath);
  }

  std::vector<DirectEdit> edits;
  edits.push_back({oldPath, std::nullopt, std::nullopt});
  emitSubtreeEdits(newPath, *oldNode->as_table(), edits, dest);
  for (const auto& [path, value] : extraOverrides) {
    if (path.empty()) {
      return false;
    }
    appendOverrideValueEdits(path, value, m_mergedUserTable, edits);
  }
  return persistDirectEdits(edits);
}

std::string ConfigService::getWallpaperPath(const std::string& connectorName) const {
  if (const std::string bundled = firstRunWallpaperPath(); !bundled.empty()) {
    return bundled;
  }
  auto it = m_monitorWallpaperPaths.find(connectorName);
  if (it != m_monitorWallpaperPaths.end()) {
    return it->second;
  }
  return m_defaultWallpaperPath;
}

std::string ConfigService::getDefaultWallpaperPath() const {
  if (const std::string bundled = firstRunWallpaperPath(); !bundled.empty()) {
    return bundled;
  }
  return m_defaultWallpaperPath;
}

std::string ConfigService::getGreeterSyncWallpaperPath() const {
  std::string palettePath = getPaletteWallpaperPath();
  if (!palettePath.empty()) {
    return palettePath;
  }

  std::vector<std::string> connectors;
  connectors.reserve(m_monitorWallpaperPaths.size());
  for (const auto& [connector, path] : m_monitorWallpaperPaths) {
    if (!path.empty()) {
      connectors.push_back(connector);
    }
  }
  std::ranges::sort(connectors);
  for (const std::string& connector : connectors) {
    return m_monitorWallpaperPaths.at(connector);
  }
  return {};
}

void ConfigService::setWallpaperChangeCallback(ChangeCallback callback) {
  m_wallpaperChangeCallback = std::move(callback);
}

void ConfigService::setWallpaperPath(const std::optional<std::string>& connectorName, const std::string& path) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_stateStore.path().empty()) {
    return;
  }

  const std::string expandedPath = FileUtils::expandUserPath(path).string();
  std::vector<std::pair<std::string_view, std::string_view>> stringEntries;
  std::vector<std::pair<std::string_view, std::string_view>> mapEntries;

  const bool monitorScoped = connectorName.has_value() && !connectorName->empty();
  if (monitorScoped) {
    auto it = m_monitorWallpaperPaths.find(*connectorName);
    if (it == m_monitorWallpaperPaths.end() || it->second != expandedPath) {
      mapEntries.emplace_back(*connectorName, path);
    }
  } else if (m_defaultWallpaperPath != expandedPath) {
    stringEntries.emplace_back("default", path);
  }

  if (m_lastWallpaperPath != expandedPath) {
    stringEntries.emplace_back("last", path);
  }

  if (stringEntries.empty() && mapEntries.empty()) {
    return;
  }

  if (!m_stateStore.setValues(
          "wallpaper", stringEntries, mapEntries.empty() ? std::string_view{} : std::string_view{"monitors"}, mapEntries
      )) {
    return;
  }

  if (monitorScoped) {
    m_monitorWallpaperPaths[*connectorName] = expandedPath;
  } else if (m_defaultWallpaperPath != expandedPath) {
    m_defaultWallpaperPath = expandedPath;
  }
  m_lastWallpaperPath = expandedPath;

  if (m_wallpaperBatchDepth > 0) {
    m_wallpaperBatchDirty = true;
    return;
  }
  if (m_wallpaperChangeCallback) {
    m_wallpaperChangeCallback();
  }
}

void ConfigService::addWallpaperFavorite(std::string path, const std::optional<WallpaperFavorite>& preset) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }

  path = FileUtils::normalizeWallpaperPath(path);
  if (path.empty()) {
    return;
  }

  std::vector<WallpaperFavorite> nextFavorites = m_wallpaperFavorites;
  std::erase_if(nextFavorites, [&](const WallpaperFavorite& favorite) { return favorite.path == path; });
  WallpaperFavorite favorite = preset.value_or(WallpaperFavorite{});
  favorite.path = std::move(path);
  nextFavorites.push_back(std::move(favorite));

  const std::vector<DirectEdit> edits{
      {{"wallpaper", "favorite"}, formatTomlValue(serializeWallpaperFavorites(nextFavorites)), std::nullopt}
  };
  (void)persistDirectEdits(edits, nullptr, false, /*reloadOnSuccess=*/false);
}

void ConfigService::removeWallpaperFavorite(std::string_view path) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }

  const std::string normalized = FileUtils::normalizeWallpaperPath(path);
  std::vector<WallpaperFavorite> nextFavorites = m_wallpaperFavorites;
  const auto before = nextFavorites.size();
  std::erase_if(nextFavorites, [&](const WallpaperFavorite& favorite) { return favorite.path == normalized; });
  if (nextFavorites.size() == before) {
    return;
  }

  std::vector<DirectEdit> edits;
  if (nextFavorites.empty()) {
    edits.push_back({{"wallpaper", "favorite"}, std::nullopt, std::nullopt});
  } else {
    edits.push_back(
        {{"wallpaper", "favorite"}, formatTomlValue(serializeWallpaperFavorites(nextFavorites)), std::nullopt}
    );
  }
  (void)persistDirectEdits(edits, nullptr, false, /*reloadOnSuccess=*/false);
}

void ConfigService::setWallpaperFavoriteThemeMode(std::string_view path, ThemeMode themeMode) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }

  const std::string normalized = FileUtils::normalizeWallpaperPath(path);
  std::vector<WallpaperFavorite> nextFavorites = m_wallpaperFavorites;
  bool changed = false;
  for (auto& favorite : nextFavorites) {
    if (favorite.path != normalized) {
      continue;
    }
    if (favorite.themeMode != themeMode) {
      favorite.themeMode = themeMode;
      changed = true;
    }
    break;
  }
  if (!changed) {
    return;
  }

  const std::vector<DirectEdit> edits{
      {{"wallpaper", "favorite"}, formatTomlValue(serializeWallpaperFavorites(nextFavorites)), std::nullopt}
  };
  (void)persistDirectEdits(edits, nullptr, false, /*reloadOnSuccess=*/false);
}

void ConfigService::setWallpaperFavoritePaletteSource(std::string_view path, std::optional<PaletteSource> source) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }

  const std::string normalized = FileUtils::normalizeWallpaperPath(path);
  std::vector<WallpaperFavorite> nextFavorites = m_wallpaperFavorites;
  bool changed = false;
  for (auto& favorite : nextFavorites) {
    if (favorite.path != normalized) {
      continue;
    }
    if (favorite.paletteSource != source) {
      favorite.paletteSource = source;
      changed = true;
    }
    if (source.has_value()) {
      switch (*source) {
      case PaletteSource::Builtin:
        if (favorite.builtinPalette.empty()) {
          favorite.builtinPalette = m_config.theme.builtinPalette;
          changed = true;
        }
        break;
      case PaletteSource::Wallpaper:
        if (favorite.wallpaperScheme.empty()) {
          favorite.wallpaperScheme = m_config.theme.wallpaperScheme;
          changed = true;
        }
        break;
      case PaletteSource::Community:
        if (favorite.communityPalette.empty()) {
          favorite.communityPalette = m_config.theme.communityPalette;
          changed = true;
        }
        break;
      case PaletteSource::Custom:
        if (favorite.customPalette.empty()) {
          favorite.customPalette = m_config.theme.customPalette;
          changed = true;
        }
        break;
      }
    }
    break;
  }
  if (!changed) {
    return;
  }

  const std::vector<DirectEdit> edits{
      {{"wallpaper", "favorite"}, formatTomlValue(serializeWallpaperFavorites(nextFavorites)), std::nullopt}
  };
  (void)persistDirectEdits(edits, nullptr, false, /*reloadOnSuccess=*/false);
}

void ConfigService::setWallpaperFavoritePaletteSelection(std::string_view path, std::string_view value) {
  if (m_settingsAccess != SettingsAccess::ReadWrite || m_configDir.empty()) {
    return;
  }

  const std::string normalized = FileUtils::normalizeWallpaperPath(path);
  const std::string selection(value);
  std::vector<WallpaperFavorite> nextFavorites = m_wallpaperFavorites;
  bool changed = false;
  for (auto& favorite : nextFavorites) {
    if (favorite.path != normalized || !favorite.paletteSource.has_value()) {
      continue;
    }
    switch (*favorite.paletteSource) {
    case PaletteSource::Builtin:
      if (favorite.builtinPalette != selection) {
        favorite.builtinPalette = selection;
        changed = true;
      }
      break;
    case PaletteSource::Wallpaper:
      if (favorite.wallpaperScheme != selection) {
        favorite.wallpaperScheme = selection;
        changed = true;
      }
      break;
    case PaletteSource::Community:
      if (favorite.communityPalette != selection) {
        favorite.communityPalette = selection;
        changed = true;
      }
      break;
    case PaletteSource::Custom:
      if (favorite.customPalette != selection) {
        favorite.customPalette = selection;
        changed = true;
      }
      break;
    }
    break;
  }
  if (!changed) {
    return;
  }

  const std::vector<DirectEdit> edits{
      {{"wallpaper", "favorite"}, formatTomlValue(serializeWallpaperFavorites(nextFavorites)), std::nullopt}
  };
  (void)persistDirectEdits(edits, nullptr, false, /*reloadOnSuccess=*/false);
}

void ConfigService::applyWallpaperSelection(
    const std::optional<std::string>& connectorName, const std::string& path, const WallpaperFavorite* applyTheme,
    const std::vector<std::string>& allConnectors
) {
  if (m_settingsAccess != SettingsAccess::ReadWrite) {
    return;
  }

  std::vector<DirectEdit> themeEdits;

  if (applyTheme != nullptr) {
    if (applyTheme->themeMode.has_value() && m_config.theme.mode != *applyTheme->themeMode) {
      themeEdits.push_back(
          {{"theme", "mode"}, formatStringValue(enumToKey(kThemeModes, *applyTheme->themeMode)), std::nullopt}
      );
    }

    if (applyTheme->paletteSource.has_value()) {
      const PaletteSource source = *applyTheme->paletteSource;
      if (m_config.theme.source != source) {
        themeEdits.push_back(
            {{"theme", "source"}, formatStringValue(enumToKey(kPaletteSources, source)), std::nullopt}
        );
      }
      switch (source) {
      case PaletteSource::Builtin:
        if (!applyTheme->builtinPalette.empty() && m_config.theme.builtinPalette != applyTheme->builtinPalette) {
          themeEdits.push_back({{"theme", "builtin"}, formatStringValue(applyTheme->builtinPalette), std::nullopt});
        }
        break;
      case PaletteSource::Wallpaper:
        if (!applyTheme->wallpaperScheme.empty() && m_config.theme.wallpaperScheme != applyTheme->wallpaperScheme) {
          themeEdits.push_back(
              {{"theme", "wallpaper_scheme"}, formatStringValue(applyTheme->wallpaperScheme), std::nullopt}
          );
        }
        break;
      case PaletteSource::Community:
        if (!applyTheme->communityPalette.empty() && m_config.theme.communityPalette != applyTheme->communityPalette) {
          themeEdits.push_back(
              {{"theme", "community_palette"}, formatStringValue(applyTheme->communityPalette), std::nullopt}
          );
        }
        break;
      case PaletteSource::Custom:
        if (!applyTheme->customPalette.empty() && m_config.theme.customPalette != applyTheme->customPalette) {
          themeEdits.push_back(
              {{"theme", "custom_palette"}, formatStringValue(applyTheme->customPalette), std::nullopt}
          );
        }
        break;
      }
    }
  }

  const std::string expandedPath = FileUtils::expandUserPath(path).string();
  std::vector<std::pair<std::string_view, std::string_view>> stringEntries;
  std::vector<std::pair<std::string_view, std::string_view>> mapEntries;
  const bool monitorScoped = connectorName.has_value() && !connectorName->empty();

  if (monitorScoped) {
    auto it = m_monitorWallpaperPaths.find(*connectorName);
    if (it == m_monitorWallpaperPaths.end() || it->second != expandedPath) {
      mapEntries.emplace_back(*connectorName, path);
    }
  } else {
    for (const auto& connector : allConnectors) {
      if (connector.empty()) {
        continue;
      }
      auto it = m_monitorWallpaperPaths.find(connector);
      if (it == m_monitorWallpaperPaths.end() || it->second != expandedPath) {
        mapEntries.emplace_back(connector, path);
      }
    }
    if (m_defaultWallpaperPath != expandedPath) {
      stringEntries.emplace_back("default", path);
    }
  }

  if (m_lastWallpaperPath != expandedPath) {
    stringEntries.emplace_back("last", path);
  }

  if (themeEdits.empty() && stringEntries.empty() && mapEntries.empty()) {
    return;
  }

  bool themePersisted = false;
  if (!themeEdits.empty()) {
    if (m_configDir.empty() || !persistDirectEdits(themeEdits, &themePersisted, false, /*reloadOnSuccess=*/false)) {
      return;
    }
  }

  bool wallpaperPersisted = false;
  if (!stringEntries.empty() || !mapEntries.empty()) {
    if (!m_stateStore.path().empty()
        && m_stateStore.setValues(
            "wallpaper", stringEntries, mapEntries.empty() ? std::string_view{} : std::string_view{"monitors"},
            mapEntries
        )) {
      if (monitorScoped) {
        m_monitorWallpaperPaths[*connectorName] = expandedPath;
      } else {
        for (const auto& [connector, _] : mapEntries) {
          m_monitorWallpaperPaths[std::string(connector)] = expandedPath;
        }
        if (m_defaultWallpaperPath != expandedPath) {
          m_defaultWallpaperPath = expandedPath;
        }
      }
      m_lastWallpaperPath = expandedPath;
      wallpaperPersisted = true;
    }
  }

  if (themePersisted) {
    loadAll();
    fireReloadCallbacks();
  }

  if ((themePersisted || wallpaperPersisted) && m_wallpaperChangeCallback) {
    m_wallpaperChangeCallback();
  }
}
