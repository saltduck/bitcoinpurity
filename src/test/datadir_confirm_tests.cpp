// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparamsbase.h>
#include <common/args.h>
#include <common/settings.h>
#include <node/datadir_confirm.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/fs.h>

#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {

class RestoreChain
{
public:
    ~RestoreChain() { SelectBaseParams(ChainType::REGTEST); }
};

struct ArgList {
    std::vector<std::string> storage{"bitcoind"};
    std::vector<const char*> ptrs;
    explicit ArgList(std::initializer_list<std::string> extra)
    {
        storage.insert(storage.end(), extra.begin(), extra.end());
        ptrs.reserve(storage.size());
        for (const std::string& arg : storage) ptrs.push_back(arg.c_str());
    }
};

void FillArgs(ArgsManager& args, std::initializer_list<std::string> extra)
{
    SetupChainParamsBaseOptions(args);
    args.AddArg("-datadir=<dir>", "", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    args.AddArg("-conf=<file>", "", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    args.AddArg("-settings=<file>", "", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    args.AddArg("-service-mode", "", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    args.AddArg("-daemon", "", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    ArgList argv{extra};
    std::string error;
    BOOST_REQUIRE_MESSAGE(args.ParseParameters(argv.ptrs.size(), argv.ptrs.data(), error), error);
}

void WriteText(const fs::path& path, const std::string& contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream file{path};
    file << contents;
    BOOST_REQUIRE(file.good());
}

node::DataDirPromptIo Io(bool terminal, std::string response = "", bool systemd = false, bool launchd = false)
{
    node::DataDirPromptIo io;
    io.stdin_is_terminal = [terminal] { return terminal; };
    io.read_line = [response = std::move(response)](std::string& line) {
        if (response == "EOF") return false;
        line = response;
        return true;
    };
    io.systemd_service = [systemd] { return systemd; };
    io.launchd_service = [launchd] { return launchd; };
    return io;
}

fs::path NetDir(const fs::path& base, ChainType chain)
{
    SelectBaseParams(chain);
    switch (chain) {
    case ChainType::TESTNET: return base / "testnet3";
    case ChainType::REGTEST: return base / "regtest";
    case ChainType::MAIN: return base;
    default: return base;
    }
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(datadir_confirm_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(marker_schema)
{
    BOOST_CHECK_EQUAL(common::ParsePurityCliDatadirStatus(common::MakePurityCliDatadirMarker("created")).value_or(""), "created");
    BOOST_CHECK_EQUAL(common::ParsePurityCliDatadirStatus(common::MakePurityCliDatadirMarker("approved")).value_or(""), "approved");

    UniValue string_schema{UniValue::VOBJ};
    string_schema.pushKV("schema", "1");
    string_schema.pushKV("status", "approved");
    BOOST_CHECK(!common::ParsePurityCliDatadirStatus(string_schema));

    UniValue extra{common::MakePurityCliDatadirMarker("approved")};
    extra.pushKV("datadir", "/tmp/elsewhere");
    BOOST_CHECK(!common::ParsePurityCliDatadirStatus(extra));

    UniValue bad_status{UniValue::VOBJ};
    bad_status.pushKV("schema", 1);
    bad_status.pushKV("status", "yes");
    BOOST_CHECK(!common::ParsePurityCliDatadirStatus(bad_status));

    BOOST_CHECK(node::IsSystemdInvocationId("0123456789abcdef0123456789abcdef"));
    BOOST_CHECK(node::IsSystemdInvocationId("0123456789ABCDEF0123456789ABCDEF"));
    BOOST_CHECK(!node::IsSystemdInvocationId("0123456789abcdef0123456789abcde"));
    BOOST_CHECK(!node::IsSystemdInvocationId("0123456789abcdef0123456789abcdeg"));
    BOOST_CHECK(!node::IsSystemdInvocationId(""));

    BOOST_CHECK(node::IsLaunchdManagedProcess(1, 1, "org.bitcoin.bitcoind"));
    BOOST_CHECK(!node::IsLaunchdManagedProcess(1, 1, "0"));
    BOOST_CHECK(!node::IsLaunchdManagedProcess(1, 1, ""));
    BOOST_CHECK(!node::IsLaunchdManagedProcess(1, 1, nullptr));
    // nohup can be reparented to pid 1 and still keep the shell session.
    BOOST_CHECK(!node::IsLaunchdManagedProcess(1, 64432, "org.bitcoin.bitcoind"));
    // ssh and a wrapper script's child are not the launchd job itself.
    BOOST_CHECK(!node::IsLaunchdManagedProcess(123, 1, "org.bitcoin.bitcoind"));
    BOOST_CHECK(!node::IsLaunchdManagedProcess(123, 123, "0"));
}

BOOST_AUTO_TEST_CASE(new_and_empty_directories_need_no_prompt)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-new"};
    fs::create_directories(base);
    SelectBaseParams(ChainType::REGTEST);

    {
        ArgsManager args;
        FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest"});
        const auto result{node::ConfirmDataDirFirstUse(args, Io(false))};
        BOOST_CHECK(!result.error);
        BOOST_CHECK_EQUAL(result.persist_status.value_or(""), "created");
    }
    fs::create_directories(base / "regtest");
    {
        ArgsManager args;
        FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest"});
        const auto result{node::ConfirmDataDirFirstUse(args, Io(false))};
        BOOST_CHECK(!result.error);
        BOOST_CHECK_EQUAL(result.persist_status.value_or(""), "created");
    }
    {
        ArgsManager args;
        FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest", "-nosettings"});
        const auto result{node::ConfirmDataDirFirstUse(args, Io(false))};
        BOOST_CHECK(!result.error);
        BOOST_CHECK(!result.persist_status);
    }
}

BOOST_AUTO_TEST_CASE(non_empty_directory_requires_explicit_confirmation)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-nonempty"};
    const fs::path net{base / "regtest"};
    WriteText(net / ".hidden", "keep");
    WriteText(net / "bitcoin.conf", "server=1\n");
    fs::create_directories(net / "wallets");
    fs::create_directories(net / "blocks");
    WriteText(net / "settings.json", "{\n  \"sync-mode\": \"p2p\"\n}\n");
    SelectBaseParams(ChainType::REGTEST);
    ArgsManager args;
    FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest"});

    const auto declined{node::ConfirmDataDirFirstUse(args, Io(false))};
    BOOST_REQUIRE(declined.error);
    BOOST_CHECK(declined.error->message.original.find(fs::PathToString(fs::absolute(net))) != std::string::npos);
    BOOST_CHECK(declined.error->message.original.find("not been confirmed") != std::string::npos);
    BOOST_CHECK(!declined.persist_status);

    for (const std::string& answer : {"n", "", "nope", "yes", "EOF"}) {
        const auto result{node::ConfirmDataDirFirstUse(args, Io(true, answer))};
        BOOST_CHECK_MESSAGE(result.error, answer);
        BOOST_CHECK(result.error->message.original.find("not accepted") != std::string::npos);
        BOOST_CHECK(!result.persist_status);
    }
    for (const std::string& answer : {"y", "Y", " y "}) {
        const auto result{node::ConfirmDataDirFirstUse(args, Io(true, answer))};
        BOOST_CHECK(!result.error);
        BOOST_CHECK_EQUAL(result.persist_status.value_or(""), "approved");
    }
}

BOOST_AUTO_TEST_CASE(valid_marker_and_unrelated_files)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-marker"};
    const fs::path net{base / "regtest"};
    WriteText(net / "blocks" / "blk00000.dat", "data");
    WriteText(net / "settings.json",
              "{\n"
              "  \"sync-mode\": \"p2p\",\n"
              "  \"purity-cli-datadir\": {\"schema\": 1, \"status\": \"created\"}\n"
              "}\n");
    SelectBaseParams(ChainType::REGTEST);
    ArgsManager created;
    FillArgs(created, {"-datadir=" + fs::PathToString(base), "-regtest"});
    const auto created_result{node::ConfirmDataDirFirstUse(created, Io(false))};
    BOOST_CHECK(!created_result.error);
    BOOST_CHECK(!created_result.persist_status);

    WriteText(net / "settings.json", "{\"purity-cli-datadir\": {\"schema\": 1, \"status\": \"approved\"}}\n");
    const auto approved{node::ConfirmDataDirFirstUse(created, Io(false))};
    BOOST_CHECK(!approved.error);
    BOOST_CHECK(!approved.persist_status);

    const fs::path moved{m_args.GetDataDirBase() / "confirm-marker-moved"};
    fs::rename(base, moved);
    ArgsManager moved_args;
    FillArgs(moved_args, {"-datadir=" + fs::PathToString(moved), "-regtest"});
    const auto moved_result{node::ConfirmDataDirFirstUse(moved_args, Io(false))};
    BOOST_CHECK(!moved_result.error);
    BOOST_CHECK(!moved_result.persist_status);
}

BOOST_AUTO_TEST_CASE(malformed_marker_does_not_bypass_confirmation)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-malformed"};
    const fs::path net{base / "regtest"};
    SelectBaseParams(ChainType::REGTEST);
    for (const char* json : {
             "{\"purity-cli-datadir\": {\"schema\": \"1\", \"status\": \"approved\"}}\n",
             "{\"purity-cli-datadir\": {\"schema\": 1, \"status\": \"approved\", \"datadir\": \"/tmp\"}}\n",
             "{\"purity-cli-datadir\": {\"schema\": 1, \"status\": \"yes\"}}\n",
             "{\"purity-cli-datadir\": []}\n",
         }) {
        WriteText(net / "note", "x");
        WriteText(net / "settings.json", json);
        ArgsManager args;
        FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest"});
        const auto result{node::ConfirmDataDirFirstUse(args, Io(false))};
        BOOST_CHECK_MESSAGE(result.error, json);
    }
}

BOOST_AUTO_TEST_CASE(corrupt_settings_keep_existing_error_path)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-corrupt"};
    WriteText(base / "regtest" / "note", "x");
    WriteText(base / "regtest" / "settings.json", "not json");
    SelectBaseParams(ChainType::REGTEST);
    ArgsManager args;
    FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest"});
    const auto result{node::ConfirmDataDirFirstUse(args, Io(false))};
    BOOST_CHECK(!result.error);
    BOOST_CHECK(!result.persist_status);
}

BOOST_AUTO_TEST_CASE(service_mode_skips_without_recording_approval)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-service"};
    WriteText(base / "regtest" / "note", "x");
    SelectBaseParams(ChainType::REGTEST);

    ArgsManager cli;
    FillArgs(cli, {"-datadir=" + fs::PathToString(base), "-regtest", "-service-mode=1"});
    const auto service{node::ConfirmDataDirFirstUse(cli, Io(false))};
    BOOST_CHECK(!service.error);
    BOOST_CHECK(!service.persist_status);

    ArgsManager daemon_only;
    FillArgs(daemon_only, {"-datadir=" + fs::PathToString(base), "-regtest", "-daemon"});
    const auto manual{node::ConfirmDataDirFirstUse(daemon_only, Io(false))};
    BOOST_REQUIRE(manual.error);
    BOOST_CHECK(manual.error->message.original.find("not been confirmed") != std::string::npos);

    ArgsManager systemd;
    FillArgs(systemd, {"-datadir=" + fs::PathToString(base), "-regtest"});
    const auto detected{node::ConfirmDataDirFirstUse(systemd, Io(false, "", true))};
    BOOST_CHECK(!detected.error);
    BOOST_CHECK(!detected.persist_status);

    ArgsManager launchd;
    FillArgs(launchd, {"-datadir=" + fs::PathToString(base), "-regtest"});
    const auto launchd_job{node::ConfirmDataDirFirstUse(launchd, Io(false, "", false, true))};
    BOOST_CHECK(!launchd_job.error);
    BOOST_CHECK(!launchd_job.persist_status);
}

BOOST_AUTO_TEST_CASE(configuration_service_mode_is_ignored)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-conf-service"};
    WriteText(base / "regtest" / "note", "x");
    WriteText(base / "bitcoin.conf", "service-mode=1\n");
    SelectBaseParams(ChainType::REGTEST);
    ArgsManager args;
    FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest"});
    std::string error;
    BOOST_REQUIRE(args.ReadConfigFiles(error, /*ignore_invalid_keys=*/true));
    const auto result{node::ConfirmDataDirFirstUse(args, Io(false))};
    BOOST_REQUIRE(result.error);
    BOOST_CHECK(result.error->message.original.find("configuration file is ignored") != std::string::npos);
    BOOST_CHECK(!result.persist_status);
}

BOOST_AUTO_TEST_CASE(external_settings_cannot_reuse_approval)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-external"};
    const fs::path shared{m_args.GetDataDirBase() / "shared-settings.json"};
    WriteText(base / "regtest" / "note", "x");
    WriteText(shared, "{\"purity-cli-datadir\": {\"schema\": 1, \"status\": \"approved\"}}\n");
    SelectBaseParams(ChainType::REGTEST);
    ArgsManager args;
    FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest", "-settings=" + fs::PathToString(shared)});

    const auto ignored{node::ConfirmDataDirFirstUse(args, Io(false))};
    BOOST_CHECK(ignored.error);
    const auto accepted{node::ConfirmDataDirFirstUse(args, Io(true, "y"))};
    BOOST_CHECK(!accepted.error);
    BOOST_CHECK(!accepted.persist_status);
}

BOOST_AUTO_TEST_CASE(network_markers_are_independent)
{
    RestoreChain restore;
    const fs::path base{m_args.GetDataDirBase() / "confirm-networks"};
    WriteText(base / "regtest" / "note", "x");
    WriteText(base / "regtest" / "settings.json", "{\"purity-cli-datadir\": {\"schema\": 1, \"status\": \"approved\"}}\n");
    WriteText(base / "testnet3" / "note", "x");
    fs::create_directories(base);

    SelectBaseParams(ChainType::REGTEST);
    ArgsManager regtest;
    FillArgs(regtest, {"-datadir=" + fs::PathToString(base), "-regtest"});
    BOOST_CHECK(!node::ConfirmDataDirFirstUse(regtest, Io(false)).error);

    SelectBaseParams(ChainType::TESTNET);
    ArgsManager testnet;
    FillArgs(testnet, {"-datadir=" + fs::PathToString(base), "-testnet"});
    const auto unconfirmed{node::ConfirmDataDirFirstUse(testnet, Io(false))};
    BOOST_CHECK(unconfirmed.error);
    BOOST_CHECK(unconfirmed.error->message.original.find("testnet3") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(not_a_directory_is_an_error)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-file"};
    WriteText(base / "regtest", "not a directory");
    SelectBaseParams(ChainType::REGTEST);
    ArgsManager args;
    FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest"});
    const auto result{node::ConfirmDataDirFirstUse(args, Io(true, "y"))};
    BOOST_REQUIRE(result.error);
    BOOST_CHECK(result.error->message.original.find("not a directory") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(recheck_after_lock_fails_closed)
{
    const fs::path base{m_args.GetDataDirBase() / "confirm-recheck"};
    const fs::path net{base / "regtest"};
    WriteText(net / "note", "x");
    SelectBaseParams(ChainType::REGTEST);
    ArgsManager args;
    FillArgs(args, {"-datadir=" + fs::PathToString(base), "-regtest"});
    BOOST_CHECK(node::RecheckDataDirFirstUse(args));

    WriteText(net / "settings.json", "{\"purity-cli-datadir\": {\"schema\": 1, \"status\": \"approved\"}}\n");
    BOOST_CHECK(!node::RecheckDataDirFirstUse(args));

    ArgsManager service;
    FillArgs(service, {"-datadir=" + fs::PathToString(base), "-regtest", "-service-mode=1"});
    fs::remove(net / "settings.json");
    BOOST_CHECK(!node::RecheckDataDirFirstUse(service));

    ArgsManager no_settings;
    FillArgs(no_settings, {"-datadir=" + fs::PathToString(base), "-regtest", "-nosettings"});
    BOOST_CHECK(!node::RecheckDataDirFirstUse(no_settings));
}

BOOST_AUTO_TEST_CASE(gui_reset_preserves_cli_marker)
{
    std::map<std::string, common::SettingsValue> settings;
    settings.emplace("prune", 1000);
    settings.emplace(CONSENSUSRULES_CONFIG_NAME, "rdts");
    settings.emplace(common::PURITY_CLI_DATADIR_SETTING, common::MakePurityCliDatadirMarker("approved"));
    node::PreserveInternalRwSettings(settings);
    BOOST_CHECK(settings.count("prune") == 0);
    BOOST_CHECK(settings.count(CONSENSUSRULES_CONFIG_NAME) == 1);
    const auto status{common::ParsePurityCliDatadirStatus(settings.at(common::PURITY_CLI_DATADIR_SETTING))};
    BOOST_REQUIRE(status);
    BOOST_CHECK_EQUAL(*status, "approved");
}

BOOST_AUTO_TEST_SUITE_END()
