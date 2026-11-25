#include "config/config_service.h"
#include "ipc/ipc_service.h"
#include "util/string_utils.h"

#include <format>
#include <sstream>
#include <string>
#include <utility>

void ConfigService::registerIpc(IpcService& ipc) {
  ipc.bind(nythera::cli::msg::configReload, [this](const std::string&) -> std::string {
    forceReload();
    return "ok\n";
  });

  ipc.bind(nythera::cli::msg::configGet, [this](const std::string& args) -> std::string {
    auto tokens = StringUtils::splitShellWords(args);
    if (!tokens || tokens->size() != 1) {
      return "error: expected exactly one argument: <path>\n";
    }
    const auto& path = (*tokens)[0];
    const auto node = m_configTable->at_path(path);
    if (!node) {
      return std::string("error: path not found: ") + path + "\n";
    }
    std::ostringstream oss;
    oss << node << "\n";
    return oss.str();
  });

  ipc.bind(nythera::cli::msg::configSet, [this](const std::string& args) -> std::string {
    auto tokens = StringUtils::splitShellWords(args);
    if (!tokens || tokens->size() < 2) {
      return "error: expected arguments: <path> <value>\n";
    }
    const std::string path = (*tokens)[0];
    const std::string value = StringUtils::trim(args.substr(path.size()));

    const std::string deltaString = path + "=" + value;
    toml::table delta;
    try {
      delta = toml::parse(deltaString);
    } catch (const toml::parse_error& e) {
      return std::format("error: invalid TOML value: {}: {}\n", deltaString, e.description());
    }

    std::vector<DirectEdit> edits;
    const auto collect = [&](auto&& self, std::vector<std::string>& keyPath, const toml::table& tbl) -> void {
      if (tbl.empty() && !keyPath.empty()) {
        edits.push_back({keyPath, "{}", std::nullopt});
        return;
      }
      for (const auto& [k, v] : tbl) {
        keyPath.emplace_back(k.str());
        if (const auto* subTbl = v.as_table()) {
          self(self, keyPath, *subTbl);
        } else {
          edits.push_back({keyPath, formatTomlValue(v), std::nullopt});
        }
        keyPath.pop_back();
      }
    };
    std::vector<std::string> keyPath;
    collect(collect, keyPath, delta);

    if (edits.empty()) {
      return "ok\n";
    }
    if (!persistDirectEdits(edits, nullptr, /*rejectWarnings=*/true, /*reloadOnSuccess=*/true)) {
      return std::format("error: {}\n", m_lastMutationError.empty() ? "failed to write config" : m_lastMutationError);
    }
    return "ok\n";
  });
}
