// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/official_broadcasts.h>

#include <common/args.h>
#include <kernel/official_packages.h>
#include <logging.h>
#include <pubkey.h>
#include <util/strencodings.h>

#include <univalue.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

//! Reuse the official-packages signing key for broadcast manifests.
constexpr const char* OFFICIAL_BROADCASTS_SIGNING_PUBKEY_HEX =
    "02f7b5f924d626f3b2378d9bcaf81c944800deb1172fb13a212428c05e4cc9509a";

std::optional<CPubKey> EmbeddedSigningPubKey()
{
    const auto bytes = ParseHex(OFFICIAL_BROADCASTS_SIGNING_PUBKEY_HEX);
    if (bytes.size() != CPubKey::COMPRESSED_SIZE) {
        LogPrintf("Official broadcasts: invalid embedded signing pubkey\n");
        return std::nullopt;
    }
    CPubKey pubkey(bytes);
    if (!pubkey.IsFullyValid()) {
        LogPrintf("Official broadcasts: embedded signing pubkey is not valid\n");
        return std::nullopt;
    }
    return pubkey;
}

bool VerifyManifestSignature(const std::string& json_contents, OfficialBroadcastTrustPolicy trust_policy)
{
    if (trust_policy == OfficialBroadcastTrustPolicy::LOCAL) return true;

    const auto signing_pubkey = EmbeddedSigningPubKey();
    if (!signing_pubkey) return false;
    return VerifyOfficialPackagesManifestSignature(json_contents, *signing_pubkey);
}

bool LooksLikeIso8601Utc(std::string_view value)
{
    // Minimal shape check: YYYY-MM-DDTHH:MM:SS…Z (or with fractional seconds).
    return value.size() >= 20 && value.back() == 'Z' && value[4] == '-' && value[7] == '-' && value[10] == 'T';
}

std::optional<OfficialBroadcastNotice> ParseNotice(const UniValue& value)
{
    if (!value.isObject()) return std::nullopt;

    OfficialBroadcastNotice notice;
    notice.id = value.find_value("id").isStr() ? value.find_value("id").get_str() : "";
    notice.title = value.find_value("title").isStr() ? value.find_value("title").get_str() : "";
    notice.body = value.find_value("body").isStr() ? value.find_value("body").get_str() : "";
    notice.published_at = value.find_value("published_at").isStr()
        ? value.find_value("published_at").get_str()
        : "";
    notice.expires_at = value.find_value("expires_at").isStr()
        ? value.find_value("expires_at").get_str()
        : "";

    if (notice.id.empty() || notice.body.empty()) {
        return std::nullopt;
    }
    if (!notice.expires_at.empty() && !LooksLikeIso8601Utc(notice.expires_at)) {
        LogPrintf("Official broadcasts: ignoring notice %s with invalid expires_at\n", notice.id);
        return std::nullopt;
    }
    return notice;
}

} // namespace

std::string GetDefaultOfficialBroadcastsManifestUrl()
{
    return "https://downloads.bitcoinpurity.org/broadcasts.json";
}

std::optional<std::string> GetOfficialBroadcastsManifestUrl(const ArgsManager& args)
{
    if (args.IsArgSet("-broadcastsmanifest")) {
        return args.GetArg("-broadcastsmanifest", "");
    }
    return GetDefaultOfficialBroadcastsManifestUrl();
}

std::optional<std::vector<OfficialBroadcastNotice>> ParseOfficialBroadcastsManifest(
    const std::string& json_contents,
    const std::string& source_label,
    OfficialBroadcastTrustPolicy trust_policy)
{
    UniValue json;
    if (!json.read(json_contents) || !json.isObject()) {
        LogPrintf("Official broadcasts: failed to parse JSON from %s\n", source_label);
        return std::nullopt;
    }

    if (!VerifyManifestSignature(json_contents, trust_policy)) {
        LogPrintf("Official broadcasts: manifest signature verification failed for %s\n", source_label);
        return std::nullopt;
    }

    if (!json.exists("schema") || !json.find_value("schema").isNum() || json.find_value("schema").getInt<int>() != 1) {
        LogPrintf("Official broadcasts: unsupported schema in %s\n", source_label);
        return std::nullopt;
    }

    const UniValue notices_json = json.find_value("notices");
    if (!notices_json.isArray()) {
        LogPrintf("Official broadcasts: missing notices array in %s\n", source_label);
        return std::nullopt;
    }

    std::vector<OfficialBroadcastNotice> notices;
    std::unordered_set<std::string> seen_ids;
    for (size_t i = 0; i < notices_json.size(); ++i) {
        const auto notice = ParseNotice(notices_json[i]);
        if (!notice) continue;
        if (!seen_ids.insert(notice->id).second) {
            LogPrintf("Official broadcasts: duplicate notice id %s in %s\n", notice->id, source_label);
            continue;
        }
        notices.push_back(*notice);
    }

    return notices;
}

std::vector<OfficialBroadcastNotice> FilterUnreadOfficialBroadcasts(
    const std::vector<OfficialBroadcastNotice>& notices,
    const std::vector<std::string>& dismissed_ids,
    std::string_view now_iso8601)
{
    std::unordered_set<std::string> dismissed(dismissed_ids.begin(), dismissed_ids.end());
    std::vector<OfficialBroadcastNotice> unread;
    unread.reserve(notices.size());

    for (const auto& notice : notices) {
        if (dismissed.count(notice.id)) continue;
        if (!notice.expires_at.empty() && !now_iso8601.empty() &&
            LooksLikeIso8601Utc(now_iso8601) && notice.expires_at <= std::string(now_iso8601)) {
            continue;
        }
        unread.push_back(notice);
    }
    return unread;
}
