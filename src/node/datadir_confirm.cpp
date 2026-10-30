// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/datadir_confirm.h>

#include <bitcoin-build-config.h>
#include <chainparamsbase.h>
#include <common/args.h>
#include <logging.h>
#include <util/fs.h>
#include <util/string.h>
#include <univalue.h>
#include <util/translation.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace node {
namespace {

enum class DirState {
    Missing,
    Empty,
    NonEmpty,
    NotDirectory,
    Unreadable,
};

DirState InspectDirectory(const fs::path& path)
{
    std::error_code ec;
    const bool exists{std::filesystem::exists(path, ec)};
    if (ec) return DirState::Unreadable;
    if (!exists) return DirState::Missing;
    const bool is_directory{std::filesystem::is_directory(path, ec)};
    if (ec) return DirState::Unreadable;
    if (!is_directory) return DirState::NotDirectory;
    const bool empty{std::filesystem::is_empty(path, ec)};
    if (ec) return DirState::Unreadable;
    return empty ? DirState::Empty : DirState::NonEmpty;
}

bool SettingsAreScoped(const fs::path& net_dir, const fs::path& settings_path)
{
    const fs::path base{fs::absolute(net_dir).lexically_normal()};
    const fs::path settings{fs::absolute(settings_path).lexically_normal()};
    const fs::path relative{settings.lexically_relative(base)};
    if (relative.empty()) return false;
    for (const fs::path& part : relative) {
        if (part == "..") return false;
    }
    return true;
}

std::optional<fs::path> ScopedSettingsPath(ArgsManager& args, const fs::path& net_dir)
{
    const fs::path settings{args.GetPathArg("-settings", BITCOIN_SETTINGS_FILENAME)};
    if (settings.empty() || net_dir.empty()) return std::nullopt;
    const fs::path path{fsbridge::AbsPathJoin(fs::absolute(net_dir), settings)};
    if (!SettingsAreScoped(net_dir, path)) return std::nullopt;
    return path;
}

bool CommandLineServiceMode(ArgsManager& args)
{
    bool enabled{false};
    args.LockSettings([&](const common::Settings& settings) {
        const auto* values{common::FindKey(settings.command_line_options, "service-mode")};
        if (!values) return;
        const common::SettingsSpan span{*values};
        if (span.empty()) return;
        enabled = SettingToBool(*span.begin()).value_or(false);
    });
    return enabled;
}

bool ConfigFileRequestsServiceMode(ArgsManager& args)
{
    bool requested{false};
    args.LockSettings([&](const common::Settings& settings) {
        auto consider = [&](const std::vector<common::SettingsValue>& values) {
            const common::SettingsSpan span{values};
            if (span.empty()) return;
            if (SettingToBool(*span.begin()).value_or(false)) requested = true;
        };
        if (const auto* values = common::FindKey(settings.rw_config, "service-mode")) consider(*values);
        for (const auto& section : settings.ro_config) {
            if (const auto* values = common::FindKey(section.second, "service-mode")) consider(*values);
        }
    });
    return requested;
}

bool SystemdServiceFromEnvironment()
{
    const char* id{std::getenv("INVOCATION_ID")};
    return id && IsSystemdInvocationId(id);
}

bool LaunchdServiceFromProcess()
{
#ifdef __APPLE__
    return IsLaunchdManagedProcess(getppid(), getsid(0), std::getenv("XPC_SERVICE_NAME"));
#else
    return false;
#endif
}

bool IsAffirmative(const std::string& line)
{
    const std::string trimmed{util::TrimString(line)};
    return trimmed == "y" || trimmed == "Y";
}

bool ReadStdinLine(std::string& line)
{
    return static_cast<bool>(std::getline(std::cin, line));
}

bool StdinIsTerminal()
{
#ifdef WIN32
    return _isatty(_fileno(stdin));
#else
    return isatty(fileno(stdin));
#endif
}

std::string PromptText(const fs::path& path)
{
    return strprintf(
        "WARNING: The selected data directory is not empty:\n\n%s\n\n"
        "Existing blockchain, wallet, and configuration data\n"
        "may be modified by %s.\n\n"
        "Do you want to continue using this directory? [y/N]: ",
        fs::PathToString(path), CLIENT_NAME);
}

common::DataDirPreflight Fail(const bilingual_str& message)
{
    return common::DataDirPreflight{common::ConfigError{common::ConfigStatus::FAILED, message}, std::nullopt};
}

common::DataDirPreflight Continue(std::optional<std::string> persist_status = std::nullopt)
{
    return common::DataDirPreflight{std::nullopt, std::move(persist_status)};
}

enum class MarkerState {
    Absent,
    Valid,
    Malformed,
    SettingsUnreadable,
};

struct MarkerRead {
    MarkerState state{MarkerState::Absent};
    bool scoped{false};
};

MarkerRead ReadMarker(ArgsManager& args, const fs::path& net_dir)
{
    MarkerRead result;
    const fs::path requested{args.GetPathArg("-settings", BITCOIN_SETTINGS_FILENAME)};
    if (requested.empty()) return result; // -nosettings
    const fs::path path{fsbridge::AbsPathJoin(fs::absolute(net_dir), requested)};
    result.scoped = SettingsAreScoped(net_dir, path);
    if (!result.scoped) return result; // Do not trust a marker stored for another directory.

    std::map<std::string, common::SettingsValue> values;
    std::vector<std::string> errors;
    if (!common::ReadSettings(path, values, errors)) {
        result.state = MarkerState::SettingsUnreadable;
        return result;
    }
    const auto it{values.find(common::PURITY_CLI_DATADIR_SETTING)};
    if (it == values.end()) return result;
    if (common::ParsePurityCliDatadirStatus(it->second)) {
        result.state = MarkerState::Valid;
    } else {
        result.state = MarkerState::Malformed;
    }
    return result;
}

} // namespace

bool IsSystemdInvocationId(std::string_view id)
{
    if (id.size() != 32) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool IsLaunchdManagedProcess(long ppid, long sid, const char* xpc_service_name)
{
    if (ppid != 1 || sid != 1) return false;
    if (xpc_service_name == nullptr || xpc_service_name[0] == '\0') return false;
    return std::strcmp(xpc_service_name, "0") != 0;
}

void PreserveInternalRwSettings(std::map<std::string, common::SettingsValue>& rw_settings)
{
    std::map<std::string, common::SettingsValue> kept;
    for (const std::string& name : {CONSENSUSRULES_CONFIG_NAME, std::string{common::PURITY_CLI_DATADIR_SETTING}}) {
        if (auto it = rw_settings.find(name); it != rw_settings.end()) {
            kept.emplace(it->first, std::move(it->second));
        }
    }
    rw_settings.swap(kept);
}

common::DataDirPreflight ConfirmDataDirFirstUse(ArgsManager& args, const DataDirPromptIo& io)
{
    const fs::path net_dir{args.GetDataDirNet()};
    if (net_dir.empty()) return Continue(); // Existing datadir resolution will report the failure.

    const std::string display{fs::PathToString(fs::absolute(net_dir))};
    switch (InspectDirectory(net_dir)) {
    case DirState::Unreadable:
        return Fail(strprintf(_("Could not inspect the data directory %s. Check that it is accessible."), display));
    case DirState::NotDirectory:
        return Fail(strprintf(_("The selected data directory path is not a directory: %s"), display));
    case DirState::Missing:
    case DirState::Empty:
        // A new or empty directory needs no approval. Record that only when the
        // settings file belongs to this network directory.
        if (ScopedSettingsPath(args, net_dir)) return Continue("created");
        return Continue();
    case DirState::NonEmpty:
        break;
    }

    const MarkerRead marker{ReadMarker(args, net_dir)};
    if (marker.state == MarkerState::SettingsUnreadable) {
        // Leave the existing settings-file error path to report the corruption.
        return Continue();
    }
    if (marker.state == MarkerState::Valid) return Continue();

    const bool systemd{io.systemd_service ? io.systemd_service() : SystemdServiceFromEnvironment()};
    const bool launchd{io.launchd_service ? io.launchd_service() : LaunchdServiceFromProcess()};
    const bool service{systemd || launchd || CommandLineServiceMode(args)};
    if (service) {
        LogWarning("First-use confirmation for non-empty data directory %s was skipped because this process is a managed service. Approval was not recorded.", fs::quoted(fs::PathToString(net_dir)));
        return Continue();
    }

    const bool terminal{io.stdin_is_terminal ? io.stdin_is_terminal() : StdinIsTerminal()};
    if (!terminal) {
        bilingual_str message{strprintf(
            _("The selected data directory is not empty and has not been confirmed:\n\n%s\n\n"
              "Re-run bitcoind from an interactive terminal and answer y to confirm it. "
              "-daemon and -daemonwait do not skip this check, and redirecting standard input is not enough. "
              "systemd services are detected automatically. launchd is detected when it starts bitcoind directly. "
              "Other service managers, and a launchd job whose program is a wrapper script, must pass -service-mode=1 on the command line."),
            display)};
        if (ConfigFileRequestsServiceMode(args)) {
            message += Untranslated(" A service-mode value in the configuration file is ignored.");
        }
        return Fail(message);
    }

    tfm::format(std::cerr, "%s", PromptText(net_dir));
    std::cerr.flush();
    std::string line;
    const bool got_line{io.read_line ? io.read_line(line) : ReadStdinLine(line)};
    if (!got_line || !IsAffirmative(line)) {
        return Fail(Untranslated(strprintf("Data directory confirmation was not accepted. %s is shutting down.", CLIENT_NAME)));
    }
    // Do not write approval into a settings file that is shared with another datadir,
    // or when settings are disabled. Those launches must confirm again next time.
    if (ScopedSettingsPath(args, net_dir)) return Continue("approved");
    return Continue();
}

std::optional<bilingual_str> RecheckDataDirFirstUse(ArgsManager& args)
{
    const fs::path net_dir{args.GetDataDirNet()};
    if (net_dir.empty()) return std::nullopt;
    if (InspectDirectory(net_dir) != DirState::NonEmpty) return std::nullopt;
    if (CommandLineServiceMode(args) || SystemdServiceFromEnvironment() || LaunchdServiceFromProcess()) return std::nullopt;

    const std::optional<fs::path> settings{ScopedSettingsPath(args, net_dir)};
    if (!settings) return std::nullopt;

    std::map<std::string, common::SettingsValue> values;
    std::vector<std::string> errors;
    if (!common::ReadSettings(*settings, values, errors)) {
        return strprintf(_("Data directory confirmation could not be rechecked because %s could not be read."), fs::PathToString(*settings));
    }
    const auto it{values.find(common::PURITY_CLI_DATADIR_SETTING)};
    if (it != values.end() && common::ParsePurityCliDatadirStatus(it->second)) return std::nullopt;
    return strprintf(_("The data directory %s changed before it could be locked and has no valid first-use confirmation."), fs::PathToString(net_dir));
}

} // namespace node
