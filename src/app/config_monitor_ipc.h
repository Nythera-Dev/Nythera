#pragma once

#include "cli/schema_msg.h"
#include "config/config_monitor.h"
#include "ipc/ipc_arg_parse.h"
#include "ipc/ipc_service.h"

#include <string>

// Lets an application ask whether Shell owns its configuration alerts, so only one of them notifies.
inline void
registerConfigMonitorStatusIpc(IpcService& ipc, const nythera::config::ApplicationConfigMonitorRegistry& monitors) {
  ipc.bind(
      nythera::cli::msg::configMonitorStatus,
      [&monitors](const std::string& args) -> std::string {
        const auto parts = nythera::ipc::splitWords(args);
        if (parts.size() != 1) {
          return "error: config-monitor-status requires <app-id>\n";
        }
        return monitors.isMonitoring(parts[0]) ? "monitored\n" : "unmonitored\n";
      },
      IpcService::HandlerOptions{.actionEditorVisibility = IpcService::ActionEditorVisibility::Hidden}
  );
}
