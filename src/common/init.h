// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COMMON_INIT_H
#define BITCOIN_COMMON_INIT_H

#include <util/translation.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class ArgsManager;

namespace common {
enum class ConfigStatus {
    FAILED,       //!< Failed generically.
    FAILED_WRITE, //!< Failed to write settings.json
    ABORTED,      //!< Aborted by user
};

struct ConfigError {
    ConfigStatus status;
    bilingual_str message{};
    std::vector<std::string> details{};
};

//! Callback function to let the user decide whether to abort loading if
//! settings.json file exists and can't be parsed, or to ignore the error and
//! overwrite the file.
using SettingsAbortFn = std::function<bool(const bilingual_str& message, const std::vector<std::string>& details)>;

//! Result of a data-directory first-use check. Runs after the effective
//! datadir is known and before InitConfig creates directories or settings.
struct DataDirPreflight {
    std::optional<ConfigError> error;
    //! When set to "created" or "approved", merge that marker into rw_settings
    //! before the settings file is written. Leave unset to keep the file unchanged.
    std::optional<std::string> persist_status;
};
using DataDirPreflightFn = std::function<DataDirPreflight(ArgsManager& args)>;

/* Read config files, and create datadir and settings.json if they don't exist.
 * preflight, when set, runs after configuration and network resolution and
 * before any directory creation or settings write.
 */
std::optional<ConfigError> InitConfig(ArgsManager& args, SettingsAbortFn settings_abort_fn = nullptr, DataDirPreflightFn preflight = nullptr);
} // namespace common

#endif // BITCOIN_COMMON_INIT_H
