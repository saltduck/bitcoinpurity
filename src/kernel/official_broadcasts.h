// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_OFFICIAL_BROADCASTS_H
#define BITCOIN_KERNEL_OFFICIAL_BROADCASTS_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ArgsManager;

static constexpr bool DEFAULT_CHECKFORBROADCASTS{true};

enum class OfficialBroadcastTrustPolicy {
    REMOTE_SIGNED,
    LOCAL,
};

/** A single official notice from the signed broadcasts manifest. */
struct OfficialBroadcastNotice {
    std::string id;
    std::string title;
    std::string body; //!< Plain-text notification body shown to the user.
    std::string published_at;
    std::string expires_at; //!< Empty when the notice never expires.
};

/** Default remote JSON URL for signed official broadcasts. */
std::string GetDefaultOfficialBroadcastsManifestUrl();

/** Optional override from -broadcastsmanifest. */
std::optional<std::string> GetOfficialBroadcastsManifestUrl(const ArgsManager& args);

/**
 * Parse and optionally verify a signed broadcasts.json manifest.
 *
 * Returns nullopt when the JSON is invalid or (under REMOTE_SIGNED) the
 * signature fails. Individual notices with missing required fields are skipped.
 */
std::optional<std::vector<OfficialBroadcastNotice>> ParseOfficialBroadcastsManifest(
    const std::string& json_contents,
    const std::string& source_label,
    OfficialBroadcastTrustPolicy trust_policy = OfficialBroadcastTrustPolicy::REMOTE_SIGNED);

/**
 * Return notices that are not dismissed and not expired (expires_at as
 * ISO-8601 UTC compared lexicographically against now_iso8601 when both look
 * like timestamps). Pass an empty now_iso8601 to skip expiry filtering.
 */
std::vector<OfficialBroadcastNotice> FilterUnreadOfficialBroadcasts(
    const std::vector<OfficialBroadcastNotice>& notices,
    const std::vector<std::string>& dismissed_ids,
    std::string_view now_iso8601 = {});

#endif // BITCOIN_KERNEL_OFFICIAL_BROADCASTS_H
