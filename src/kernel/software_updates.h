// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_SOFTWARE_UPDATES_H
#define BITCOIN_KERNEL_SOFTWARE_UPDATES_H

#include <uint256.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ArgsManager;

static constexpr bool DEFAULT_CHECKFORUPDATES{true};

enum class SoftwareUpdateUrgency {
    // Not named OPTIONAL: Windows headers define OPTIONAL as an empty macro.
    OPTIONAL_UPDATE,
    RECOMMENDED,
    REQUIRED,
};

enum class SoftwareUpdateTrustPolicy {
    REMOTE_SIGNED,
    LOCAL,
};

struct SoftwareReleaseArtifact {
    std::string platform;
    std::string download_uri;
    uint256 archive_sha256;
    uint64_t archive_size_bytes{0};
};

struct SoftwareReleaseInfo {
    std::string version;
    std::string released_at;
    SoftwareUpdateUrgency urgency{SoftwareUpdateUrgency::OPTIONAL_UPDATE};
    std::string release_notes_url;
    std::vector<SoftwareReleaseArtifact> artifacts;
};

/** Default remote JSON URL for signed release manifests. */
std::string GetDefaultSoftwareUpdatesManifestUrl();

/** Optional override from -updatesmanifest. */
std::optional<std::string> GetSoftwareUpdatesManifestUrl(const ArgsManager& args);

/** Compile-time platform identifier used to select a release artifact. */
std::string DetectCurrentSoftwarePlatform();

/** Parse MAJOR.MINOR.PATCH[rcN][-dev…] version strings. */
std::optional<int> CompareSoftwareVersions(std::string_view lhs, std::string_view rhs);

/** Return true when remote is strictly newer than local. */
bool IsNewerSoftwareVersion(std::string_view remote, std::string_view local);

/** Whether local builds should skip automatic update prompts. */
bool ShouldSkipAutomaticSoftwareUpdatePrompt(std::string_view local_version);

/** Parse and optionally verify a signed releases.json manifest. */
std::optional<SoftwareReleaseInfo> ParseSoftwareReleaseManifest(
    const std::string& json_contents,
    const std::string& source_label,
    SoftwareUpdateTrustPolicy trust_policy = SoftwareUpdateTrustPolicy::REMOTE_SIGNED);

/** Select the artifact matching the current platform, if present. */
std::optional<SoftwareReleaseArtifact> FindSoftwareReleaseArtifact(
    const SoftwareReleaseInfo& release, std::string_view platform);

/** Whether a download URI satisfies the trust policy (HTTPS + host allowlist for REMOTE_SIGNED). */
bool IsSoftwareUpdateDownloadUriAllowed(const std::string& uri, SoftwareUpdateTrustPolicy trust_policy);

#endif // BITCOIN_KERNEL_SOFTWARE_UPDATES_H
