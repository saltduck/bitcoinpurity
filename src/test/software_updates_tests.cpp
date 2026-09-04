// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/software_updates.h>

#include <key.h>
#include <kernel/official_packages.h>
#include <util/strencodings.h>

#include <univalue.h>

#include <string>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

namespace {

std::string SignManifest(const CKey& key, const std::string& json_contents)
{
    const uint256 digest = OfficialPackagesManifestDigest(json_contents);
    std::vector<unsigned char> signature;
    BOOST_REQUIRE(key.Sign(digest, signature));
    return EncodeBase64(signature);
}

std::string AddManifestSignature(const std::string& unsigned_json, const std::string& signature_b64)
{
    UniValue json;
    BOOST_REQUIRE(json.read(unsigned_json));
    json.pushKV("signature", signature_b64);
    return json.write(0, 0);
}

const std::string SAMPLE_UNSIGNED_MANIFEST = R"({
  "schema": 1,
  "latest": {
    "version": "1.0.1",
    "released_at": "2026-09-02T00:00:00Z",
    "urgency": "optional",
    "release_notes_url": "https://github.com/saltduck/bitcoinpurity/releases/tag/v1.0.1",
    "artifacts": [
      {
        "platform": "linux-x86_64",
        "download_uri": "https://github.com/saltduck/bitcoinpurity/releases/download/v1.0.1/bitcoin-purity-1.0.1-x86_64-linux-gnu.tar.gz",
        "archive_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "archive_size_bytes": 123456789
      }
    ]
  }
})";

} // namespace

BOOST_FIXTURE_TEST_SUITE(software_updates_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(version_compare)
{
    const auto lt = CompareSoftwareVersions("1.0.0", "1.0.1");
    BOOST_REQUIRE(lt);
    BOOST_CHECK_EQUAL(*lt, -1);

    const auto gt = CompareSoftwareVersions("1.1.0", "1.0.9");
    BOOST_REQUIRE(gt);
    BOOST_CHECK_EQUAL(*gt, 1);

    const auto eq = CompareSoftwareVersions("1.0.0", "1.0.0");
    BOOST_REQUIRE(eq);
    BOOST_CHECK_EQUAL(*eq, 0);

    BOOST_CHECK(CompareSoftwareVersions("1.0.0rc1", "1.0.0rc2").value_or(0) < 0);
    BOOST_CHECK(CompareSoftwareVersions("1.0.0rc2", "1.0.0").value_or(0) < 0);
    BOOST_CHECK(CompareSoftwareVersions("1.0.0", "1.0.0rc9").value_or(0) > 0);

    BOOST_CHECK(IsNewerSoftwareVersion("1.0.1", "1.0.0"));
    BOOST_CHECK(!IsNewerSoftwareVersion("1.0.0", "1.0.1"));
    BOOST_CHECK(!IsNewerSoftwareVersion("1.0.0rc1", "v1.0.0rc1-4143c14706e8"));
    BOOST_CHECK(IsNewerSoftwareVersion("1.0.0rc2", "v1.0.0rc1-4143c14706e8"));
    BOOST_CHECK(IsNewerSoftwareVersion("1.0.1", "v1.0.0rc1-4143c14706e8"));
    BOOST_CHECK(ShouldSkipAutomaticSoftwareUpdatePrompt("1.0.0-dev-a83f921"));
    BOOST_CHECK(!ShouldSkipAutomaticSoftwareUpdatePrompt("1.0.0"));
    BOOST_CHECK(!ShouldSkipAutomaticSoftwareUpdatePrompt("v1.0.0rc1-4143c14706e8"));
}

BOOST_AUTO_TEST_CASE(download_uri_policy)
{
    BOOST_CHECK(IsSoftwareUpdateDownloadUriAllowed(
        "https://github.com/saltduck/bitcoinpurity/releases/download/v1.0.1/pkg.tar.gz",
        SoftwareUpdateTrustPolicy::REMOTE_SIGNED));
    BOOST_CHECK(IsSoftwareUpdateDownloadUriAllowed(
        "https://release-assets.githubusercontent.com/github-production-release-asset/example",
        SoftwareUpdateTrustPolicy::REMOTE_SIGNED));
    BOOST_CHECK(!IsSoftwareUpdateDownloadUriAllowed(
        "http://github.com/saltduck/bitcoinpurity/releases/download/v1.0.1/pkg.tar.gz",
        SoftwareUpdateTrustPolicy::REMOTE_SIGNED));
    BOOST_CHECK(!IsSoftwareUpdateDownloadUriAllowed(
        "https://evil.example/pkg.tar.gz",
        SoftwareUpdateTrustPolicy::REMOTE_SIGNED));
    BOOST_CHECK(IsSoftwareUpdateDownloadUriAllowed(
        "http://localhost/pkg.tar.gz",
        SoftwareUpdateTrustPolicy::LOCAL));
}

BOOST_AUTO_TEST_CASE(parse_signed_manifest)
{
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();

    const std::string signed_json = AddManifestSignature(SAMPLE_UNSIGNED_MANIFEST, SignManifest(key, SAMPLE_UNSIGNED_MANIFEST));
    const auto release = ParseSoftwareReleaseManifest(
        signed_json, "test", SoftwareUpdateTrustPolicy::LOCAL);
    BOOST_REQUIRE(release);
    BOOST_CHECK_EQUAL(release->version, "1.0.1");
    BOOST_CHECK_EQUAL(release->urgency, SoftwareUpdateUrgency::OPTIONAL_UPDATE);
    BOOST_REQUIRE_EQUAL(release->artifacts.size(), 1U);
    BOOST_CHECK_EQUAL(release->artifacts[0].platform, "linux-x86_64");
    BOOST_CHECK(VerifyOfficialPackagesManifestSignature(signed_json, pubkey));

    const auto artifact = FindSoftwareReleaseArtifact(*release, "linux-x86_64");
    BOOST_REQUIRE(artifact);
    BOOST_CHECK_EQUAL(artifact->archive_size_bytes, 123456789ULL);
    BOOST_CHECK(!FindSoftwareReleaseArtifact(*release, "windows-x86_64"));
}

BOOST_AUTO_TEST_CASE(remote_manifest_requires_signature)
{
    const auto release = ParseSoftwareReleaseManifest(
        SAMPLE_UNSIGNED_MANIFEST, "test", SoftwareUpdateTrustPolicy::REMOTE_SIGNED);
    BOOST_CHECK(!release);

    const auto local_release = ParseSoftwareReleaseManifest(
        SAMPLE_UNSIGNED_MANIFEST, "test", SoftwareUpdateTrustPolicy::LOCAL);
    BOOST_REQUIRE(local_release);
    BOOST_CHECK_EQUAL(local_release->version, "1.0.1");
}

BOOST_AUTO_TEST_CASE(platform_detection)
{
#if defined(WIN32)
    BOOST_CHECK_EQUAL(DetectCurrentSoftwarePlatform(), "windows-x86_64");
#elif defined(__APPLE__)
    BOOST_CHECK_EQUAL(DetectCurrentSoftwarePlatform(), "macos-arm64");
#elif defined(__aarch64__)
    BOOST_CHECK_EQUAL(DetectCurrentSoftwarePlatform(), "linux-arm64");
#else
    BOOST_CHECK_EQUAL(DetectCurrentSoftwarePlatform(), "linux-x86_64");
#endif
}

BOOST_AUTO_TEST_CASE(manifest_rc2_offers_upgrade_over_local_rc1_build)
{
    const std::string unsigned_json = R"({
  "schema": 1,
  "latest": {
    "version": "1.0.0rc2",
    "released_at": "2026-09-02T00:00:00Z",
    "urgency": "optional",
    "release_notes_url": "https://github.com/saltduck/bitcoinpurity/releases/tag/v1.0.0rc2",
    "artifacts": [
      {
        "platform": "macos-arm64",
        "download_uri": "https://github.com/saltduck/bitcoinpurity/releases/download/v1.0.0rc2/Bitcoin-Purity-1.0.0rc2-arm64-apple-darwin.zip",
        "archive_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "archive_size_bytes": 22239269
      }
    ]
  }
})";

    const auto release = ParseSoftwareReleaseManifest(unsigned_json, "test", SoftwareUpdateTrustPolicy::LOCAL);
    BOOST_REQUIRE(release);
    BOOST_CHECK_EQUAL(release->version, "1.0.0rc2");

    const std::string local_build_version = "v1.0.0rc1-4143c14706e8";
    BOOST_CHECK(IsNewerSoftwareVersion(release->version, local_build_version));

    const auto artifact = FindSoftwareReleaseArtifact(*release, "macos-arm64");
    BOOST_REQUIRE(artifact);
    BOOST_CHECK(artifact->download_uri.find("1.0.0rc2") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(archive_sha256_uses_standard_byte_order)
{
    const std::string hex = "06f0c3946f44c52e0c0613396114e8697d67c1f305813db32181000c90769469";
    const auto parsed = ParseStandardSha256Hex(hex);
    BOOST_REQUIRE(parsed);
    BOOST_CHECK_EQUAL(parsed->begin()[0], 0x06);
    BOOST_CHECK_EQUAL(parsed->begin()[31], 0x69);

    const std::string unsigned_json = R"({
  "schema": 1,
  "latest": {
    "version": "1.0.0rc4",
    "released_at": "2026-09-02T00:00:00Z",
    "urgency": "optional",
    "release_notes_url": "https://github.com/saltduck/bitcoinpurity/releases/tag/v1.0.0rc4",
    "artifacts": [
      {
        "platform": "macos-arm64",
        "download_uri": "https://downloads.bitcoinpurity.org/Bitcoin-Purity-1.0.0rc4-arm64-apple-darwin.zip",
        "archive_sha256": ")" + hex + R"(",
        "archive_size_bytes": 22242152
      }
    ]
  }
})";

    const auto release = ParseSoftwareReleaseManifest(unsigned_json, "test", SoftwareUpdateTrustPolicy::LOCAL);
    BOOST_REQUIRE(release);
    const auto artifact = FindSoftwareReleaseArtifact(*release, "macos-arm64");
    BOOST_REQUIRE(artifact);
    BOOST_CHECK_EQUAL(artifact->archive_sha256, *parsed);
    BOOST_CHECK(artifact->archive_sha256 != uint256::FromUserHex(hex).value());
}

BOOST_AUTO_TEST_SUITE_END()
