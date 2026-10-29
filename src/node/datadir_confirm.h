// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_DATADIR_CONFIRM_H
#define BITCOIN_NODE_DATADIR_CONFIRM_H

#include <common/init.h>
#include <common/settings.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

class ArgsManager;
class bilingual_str;

namespace node {

//! Hooks for tests. Empty functions use the real terminal, stdin, and environment.
struct DataDirPromptIo {
    std::function<bool()> stdin_is_terminal;
    std::function<bool(std::string&)> read_line;
    std::function<bool()> systemd_service;
    std::function<bool()> launchd_service;
};

//! Inspect the effective network data directory and apply the CLI first-use policy.
//! Does not create directories or write settings. persist_status is set only when
//! a scoped settings file should record "created" or "approved".
common::DataDirPreflight ConfirmDataDirFirstUse(ArgsManager& args, const DataDirPromptIo& io = {});

//! After the data directory lock is held, fail closed if a non-empty directory
//! no longer has a valid confirmation record. Service mode and disabled or
//! unscoped settings are left to the earlier check.
std::optional<bilingual_str> RecheckDataDirFirstUse(ArgsManager& args);

//! Keep internal rw_settings that GUI Reset must not delete.
void PreserveInternalRwSettings(std::map<std::string, common::SettingsValue>& rw_settings);

//! systemd's INVOCATION_ID is a 32-character hex string. Anything else is ignored.
bool IsSystemdInvocationId(std::string_view id);

//! True only for the process launchd itself started. ppid and sid must both be
//! 1, and XPC_SERVICE_NAME must be the job label. The value "0", which launchd
//! puts on ordinary GUI apps and everything they start, does not count.
//! An orphan reparented to pid 1 keeps its old session id, so nohup does not match.
bool IsLaunchdManagedProcess(long ppid, long sid, const char* xpc_service_name);

} // namespace node

#endif // BITCOIN_NODE_DATADIR_CONFIRM_H
