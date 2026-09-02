// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/software_updates.h>

#include <common/args.h>
#include <kernel/official_packages.h>
#include <logging.h>
#include <pubkey.h>
#include <util/strencodings.h>

#include <univalue.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

//! Reuse the official-packages signing key for release manifests.
constexpr const char* SOFTWARE_UPDATES_SIGNING_PUBKEY_HEX =
    "02f7b5f924d626f3b2378d9bcaf81c944800deb1172fb13a212428c05e4cc9509a";

struct ParsedSoftwareVersion {
    int major{0};
    int minor{0};
    int patch{0};
    int rc{-1};
    bool is_dev{false};
};

std::optional<CPubKey> EmbeddedSigningPubKey()
{
    const auto bytes = ParseHex(SOFTWARE_UPDATES_SIGNING_PUBKEY_HEX);
    if (bytes.size() != CPubKey::COMPRESSED_SIZE) {
        LogPrintf("Software updates: invalid embedded signing pubkey\n");
        return std::nullopt;
    }
    CPubKey pubkey(bytes);
    if (!pubkey.IsFullyValid()) {
        LogPrintf("Software updates: embedded signing pubkey is not valid\n");
        return std::nullopt;
    }
    return pubkey;
}

std::string ToLowerAscii(std::string_view input)
{
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::optional<std::string_view> ExtractUriHost(std::string_view uri)
{
    constexpr std::string_view HTTPS_PREFIX{"https://"};
    if (!uri.starts_with(HTTPS_PREFIX)) return std::nullopt;
    uri.remove_prefix(HTTPS_PREFIX.size());

    if (uri.find('@') != std::string_view::npos) return std::nullopt;
    const size_t slash = uri.find('/');
    const size_t colon = uri.find(':');
    const size_t end = std::min(slash, colon);
    if (end == 0) return std::nullopt;
    return uri.substr(0, end);
}


bool IsAllowedArtifactHost(std::string_view host)
{
    static constexpr std::string_view ALLOWED_HOSTS[] = {
        "downloads.bitcoinpurity.org",
        "github.com",
        "release-assets.githubusercontent.com",
        "objects.githubusercontent.com",
    };
    const std::string lowered = ToLowerAscii(host);
    for (const auto allowed : ALLOWED_HOSTS) {
        if (lowered == allowed) return true;
    }
    return false;
}

bool ParseUnsignedComponent(std::string_view text, int& out)
{
    if (text.empty()) return false;
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    out = value;
    return true;
}

std::optional<ParsedSoftwareVersion> ParseSoftwareVersionString(std::string_view input)
{
    if (!input.empty() && input.front() == 'v') {
        input.remove_prefix(1);
    }

    ParsedSoftwareVersion parsed;
    const auto dev_pos = input.find("-dev");
    if (dev_pos != std::string_view::npos) {
        parsed.is_dev = true;
        input = input.substr(0, dev_pos);
    }

    int rc_number = -1;
    const auto rc_pos = input.find("rc");
    if (rc_pos != std::string_view::npos) {
        const auto rc_text = input.substr(rc_pos + 2);
        if (rc_text.empty() || !ParseUnsignedComponent(rc_text, rc_number) || rc_number < 1) {
            return std::nullopt;
        }
        input = input.substr(0, rc_pos);
        parsed.rc = rc_number;
    }

    const auto first_dot = input.find('.');
    if (first_dot == std::string_view::npos) return std::nullopt;
    const auto second_dot = input.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) return std::nullopt;
    if (input.find('.', second_dot + 1) != std::string_view::npos) return std::nullopt;

    if (!ParseUnsignedComponent(input.substr(0, first_dot), parsed.major)) return std::nullopt;
    if (!ParseUnsignedComponent(input.substr(first_dot + 1, second_dot - first_dot - 1), parsed.minor)) return std::nullopt;
    if (!ParseUnsignedComponent(input.substr(second_dot + 1), parsed.patch)) return std::nullopt;
    return parsed;
}

int CompareParsedVersions(const ParsedSoftwareVersion& lhs, const ParsedSoftwareVersion& rhs)
{
    if (lhs.major != rhs.major) return lhs.major < rhs.major ? -1 : 1;
    if (lhs.minor != rhs.minor) return lhs.minor < rhs.minor ? -1 : 1;
    if (lhs.patch != rhs.patch) return lhs.patch < rhs.patch ? -1 : 1;

    const int lhs_rc = lhs.rc < 0 ? std::numeric_limits<int>::max() : lhs.rc;
    const int rhs_rc = rhs.rc < 0 ? std::numeric_limits<int>::max() : rhs.rc;
    if (lhs_rc != rhs_rc) return lhs_rc < rhs_rc ? -1 : 1;
    return 0;
}

std::optional<SoftwareUpdateUrgency> ParseUrgency(std::string_view urgency)
{
    const std::string lowered = ToLowerAscii(urgency);
    if (lowered == "optional") return SoftwareUpdateUrgency::OPTIONAL;
    if (lowered == "recommended") return SoftwareUpdateUrgency::RECOMMENDED;
    if (lowered == "required") return SoftwareUpdateUrgency::REQUIRED;
    return std::nullopt;
}

std::optional<SoftwareReleaseArtifact> ParseArtifact(const UniValue& value, SoftwareUpdateTrustPolicy trust_policy)
{
    if (!value.isObject()) return std::nullopt;

    const std::string platform = value.find_value("platform").isStr() ? value.find_value("platform").get_str() : "";
    const std::string download_uri = value.find_value("download_uri").isStr() ? value.find_value("download_uri").get_str() : "";
    const std::string archive_sha256_hex = value.find_value("archive_sha256").isStr()
        ? value.find_value("archive_sha256").get_str()
        : "";
    const int64_t archive_size_bytes = value.find_value("archive_size_bytes").isNum()
        ? value.find_value("archive_size_bytes").getInt<int64_t>()
        : 0;

    if (platform.empty() || download_uri.empty() || archive_sha256_hex.empty() || archive_size_bytes <= 0) {
        return std::nullopt;
    }
    if (!IsSoftwareUpdateDownloadUriAllowed(download_uri, trust_policy)) {
        LogPrintf("Software updates: rejected artifact download URI %s\n", download_uri);
        return std::nullopt;
    }

    const auto hash = uint256::FromUserHex(archive_sha256_hex);
    if (!hash) {
        LogPrintf("Software updates: invalid archive_sha256 for platform %s\n", platform);
        return std::nullopt;
    }

    SoftwareReleaseArtifact artifact;
    artifact.platform = platform;
    artifact.download_uri = download_uri;
    artifact.archive_sha256 = *hash;
    artifact.archive_size_bytes = static_cast<uint64_t>(archive_size_bytes);
    return artifact;
}

bool VerifyManifestSignature(const std::string& json_contents, SoftwareUpdateTrustPolicy trust_policy)
{
    if (trust_policy == SoftwareUpdateTrustPolicy::LOCAL) return true;

    const auto signing_pubkey = EmbeddedSigningPubKey();
    if (!signing_pubkey) return false;
    return VerifyOfficialPackagesManifestSignature(json_contents, *signing_pubkey);
}

} // namespace

std::string GetDefaultSoftwareUpdatesManifestUrl()
{
    return "https://downloads.bitcoinpurity.org/releases.json";
}

std::optional<std::string> GetSoftwareUpdatesManifestUrl(const ArgsManager& args)
{
    if (args.IsArgSet("-updatesmanifest")) {
        return args.GetArg("-updatesmanifest", "");
    }
    return GetDefaultSoftwareUpdatesManifestUrl();
}

std::string DetectCurrentSoftwarePlatform()
{
#if defined(WIN32)
    return "windows-x86_64";
#elif defined(__APPLE__)
    return "macos-arm64";
#elif defined(__aarch64__)
    return "linux-arm64";
#else
    return "linux-x86_64";
#endif
}

std::optional<int> CompareSoftwareVersions(std::string_view lhs, std::string_view rhs)
{
    const auto parsed_lhs = ParseSoftwareVersionString(lhs);
    const auto parsed_rhs = ParseSoftwareVersionString(rhs);
    if (!parsed_lhs || !parsed_rhs) return std::nullopt;
    return CompareParsedVersions(*parsed_lhs, *parsed_rhs);
}

bool IsNewerSoftwareVersion(std::string_view remote, std::string_view local)
{
    const auto comparison = CompareSoftwareVersions(remote, local);
    return comparison && *comparison > 0;
}

bool ShouldSkipAutomaticSoftwareUpdatePrompt(std::string_view local_version)
{
    const auto parsed = ParseSoftwareVersionString(local_version);
    return parsed && parsed->is_dev;
}

std::optional<SoftwareReleaseInfo> ParseSoftwareReleaseManifest(
    const std::string& json_contents,
    const std::string& source_label,
    SoftwareUpdateTrustPolicy trust_policy)
{
    UniValue json;
    if (!json.read(json_contents) || !json.isObject()) {
        LogPrintf("Software updates: failed to parse JSON from %s\n", source_label);
        return std::nullopt;
    }

    if (!VerifyManifestSignature(json_contents, trust_policy)) {
        LogPrintf("Software updates: manifest signature verification failed for %s\n", source_label);
        return std::nullopt;
    }

    if (!json.exists("schema") || !json.find_value("schema").isNum() || json.find_value("schema").getInt<int>() != 1) {
        LogPrintf("Software updates: unsupported schema in %s\n", source_label);
        return std::nullopt;
    }

    const UniValue latest = json.find_value("latest");
    if (!latest.isObject()) {
        LogPrintf("Software updates: missing latest object in %s\n", source_label);
        return std::nullopt;
    }

    SoftwareReleaseInfo release;
    release.version = latest.find_value("version").isStr() ? latest.find_value("version").get_str() : "";
    release.released_at = latest.find_value("released_at").isStr() ? latest.find_value("released_at").get_str() : "";
    release.release_notes_url = latest.find_value("release_notes_url").isStr()
        ? latest.find_value("release_notes_url").get_str()
        : "";

    const std::string urgency = latest.find_value("urgency").isStr() ? latest.find_value("urgency").get_str() : "optional";
    const auto parsed_urgency = ParseUrgency(urgency);
    if (!parsed_urgency || release.version.empty() || release.release_notes_url.empty()) {
        LogPrintf("Software updates: invalid latest metadata in %s\n", source_label);
        return std::nullopt;
    }
    release.urgency = *parsed_urgency;

    const UniValue artifacts = latest.find_value("artifacts");
    if (!artifacts.isArray()) {
        LogPrintf("Software updates: missing artifacts array in %s\n", source_label);
        return std::nullopt;
    }

    for (size_t i = 0; i < artifacts.size(); ++i) {
        if (const auto artifact = ParseArtifact(artifacts[i], trust_policy)) {
            release.artifacts.push_back(*artifact);
        }
    }

    if (release.artifacts.empty()) {
        LogPrintf("Software updates: no valid artifacts in %s\n", source_label);
        return std::nullopt;
    }

    return release;
}

std::optional<SoftwareReleaseArtifact> FindSoftwareReleaseArtifact(
    const SoftwareReleaseInfo& release, std::string_view platform)
{
    for (const auto& artifact : release.artifacts) {
        if (artifact.platform == platform) return artifact;
    }
    return std::nullopt;
}

bool IsSoftwareUpdateDownloadUriAllowed(const std::string& uri, SoftwareUpdateTrustPolicy trust_policy)
{
    if (trust_policy == SoftwareUpdateTrustPolicy::LOCAL) {
        return uri.starts_with("https://") || uri.starts_with("http://");
    }

    const auto host = ExtractUriHost(uri);
    if (!host) return false;
    return IsAllowedArtifactHost(*host);
}
