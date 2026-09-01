// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/official_packages.h>

#include <chainparams.h>
#include <key.h>
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

} // namespace

BOOST_FIXTURE_TEST_SUITE(official_packages_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(download_uri_policy)
{
    BOOST_CHECK(IsOfficialDownloadUriAllowed(
        "https://downloads.bitcoinpurity.org/mainnet/pkg.zip", OfficialPackageTrustPolicy::REMOTE_SIGNED));
    BOOST_CHECK(!IsOfficialDownloadUriAllowed(
        "http://downloads.bitcoinpurity.org/mainnet/pkg.zip", OfficialPackageTrustPolicy::REMOTE_SIGNED));
    BOOST_CHECK(!IsOfficialDownloadUriAllowed(
        "https://evil.example/mainnet/pkg.zip", OfficialPackageTrustPolicy::REMOTE_SIGNED));
    BOOST_CHECK(IsOfficialDownloadUriAllowed(
        "http://localhost/pkg.zip", OfficialPackageTrustPolicy::LOCAL));
}

BOOST_AUTO_TEST_CASE(zip_entry_path_safety)
{
    BOOST_CHECK(IsZipArchiveEntryPathSafe("blocks/blk00000.dat"));
    BOOST_CHECK(!IsZipArchiveEntryPathSafe("../bitcoin.conf"));
    BOOST_CHECK(!IsZipArchiveEntryPathSafe("/etc/passwd"));
    BOOST_CHECK(!IsZipArchiveEntryPathSafe("blocks/../../wallets/x"));
}

BOOST_AUTO_TEST_CASE(snapshot_trust_mainnet_assumeutxo)
{
    const auto params = CreateChainParams(m_args, ChainType::MAIN);
    BOOST_REQUIRE(params);
    const uint256 hash = uint256::FromUserHex("0000000000000000000108970acb9522ffd516eae17acddcb1bd16469194a821").value();
    BOOST_CHECK(IsOfficialSnapshotTrusted(*params, 910000, hash));
    BOOST_CHECK(!IsOfficialSnapshotTrusted(*params, 910000, uint256::ZERO));
    BOOST_CHECK(!IsOfficialSnapshotTrusted(*params, 1, hash));
}

BOOST_AUTO_TEST_CASE(snapshot_trust_allows_unpinned_post_activation_height)
{
    const auto params = CreateChainParams(m_args, ChainType::MAIN);
    BOOST_REQUIRE(params);
    const uint256 package_hash = uint256::FromUserHex("00000000000000040440db37ab428b029ee5dda57d192088c13046d124eeb80b").value();
    BOOST_CHECK(IsOfficialSnapshotTrusted(*params, 961814, package_hash));
    BOOST_CHECK(!IsOfficialSnapshotTrusted(*params, 961814, uint256::ZERO));

    const uint256 activation = uint256::FromUserHex("0000000000000000003ea74f4dafdda7ed4e02c4c1ccb9768e0ca4f9e1a35159").value();
    BOOST_CHECK(IsOfficialSnapshotTrusted(*params, 961637, activation));
    BOOST_CHECK(!IsOfficialSnapshotTrusted(*params, 961637, package_hash));
    BOOST_CHECK(!IsOfficialSnapshotTrusted(*params, 800000, package_hash));
}

BOOST_AUTO_TEST_CASE(parse_published_mainnet_manifest)
{
    // Live https://downloads.bitcoinpurity.org/official-packages-mainnet.json
    const std::string published_json = R"({
  "packages": [
    {
      "id": "mainnet-961814-prune-10gb",
      "snapshot_height": 961814,
      "base_blockhash": "00000000000000040440db37ab428b029ee5dda57d192088c13046d124eeb80b",
      "prune_mib": 10000,
      "download_uri": "https://downloads.bitcoinpurity.org/nodedata/BitcoinPurity-10000-961814-4b9ebf59.zip",
      "archive_sha256": "4b9ebf59002eda73aff911803642c0223bb792660c48f76826780836182e21f1",
      "archive_size_bytes": 30390803377,
      "extracted_size_bytes": 34182117528
    }
  ],
  "signature": "MEYCIQCcjCUf1XDcD1OGNO0bO+AyFG/wMrN7wRXjKBaMd3XsWgIhAOmIKizS3yhsdpL97oAhkm1WgdD2WCjk3fdQwzC7LTYJ"
})";

    const auto packages = ParseOfficialDataPackagesFromJson(
        published_json, "published-mainnet", OfficialPackageTrustPolicy::REMOTE_SIGNED);
    BOOST_REQUIRE_EQUAL(packages.size(), 1U);
    BOOST_CHECK_EQUAL(packages[0].id, "mainnet-961814-prune-10gb");
    BOOST_CHECK_EQUAL(packages[0].snapshot_height, 961814);
    BOOST_CHECK_EQUAL(packages[0].prune_mib, 10000);
    BOOST_CHECK_EQUAL(packages[0].archive_size_bytes, 30390803377ULL);
}

BOOST_AUTO_TEST_CASE(manifest_signature_roundtrip)
{
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();

    const std::string unsigned_json = R"({
  "packages": [
    {
      "id": "mainnet-910000-prune-2gb",
      "snapshot_height": 910000,
      "base_blockhash": "0000000000000000000108970acb9522ffd516eae17acddcb1bd16469194a821",
      "prune_mib": 1907,
      "download_uri": "https://downloads.bitcoinpurity.org/mainnet/mainnet-910000-prune-2gb.zip",
      "archive_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
  ]
})";

    const std::string signed_json = AddManifestSignature(unsigned_json, SignManifest(key, unsigned_json));
    BOOST_CHECK(VerifyOfficialPackagesManifestSignature(signed_json, pubkey));
    BOOST_CHECK(!VerifyOfficialPackagesManifestSignature(unsigned_json, pubkey));
}

// Cross-implementation vector: manifest signed with
// contrib/official-packages/sign-manifest.py (openssl pkeyutl over the
// canonical single-SHA256 digest) must verify with CPubKey::Verify().
BOOST_AUTO_TEST_CASE(manifest_signature_python_tool_vector)
{
    const std::string signed_json = R"({
  "packages": [
    {
      "id": "mainnet-910000-prune-2gb",
      "snapshot_height": 910000,
      "base_blockhash": "0000000000000000000108970acb9522ffd516eae17acddcb1bd16469194a821",
      "prune_mib": 1907,
      "download_uri": "https://downloads.bitcoinpurity.org/mainnet/mainnet-910000-prune-2gb.zip",
      "archive_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
  ],
  "signature": "MEQCIG+kvW+rrCkdKQ/0G3UePl9IHnOiaZeDTvsoa2jqxwMNAiAUtHGgE01pso95PeBdABvF4T/BioDbpb27UjIqrn3mRg=="
})";

    const CPubKey pubkey{ParseHex("0234b9bee991cfee1181eb994606ef744517c73365840870e44f4889c65bc09a39")};
    BOOST_REQUIRE(pubkey.IsFullyValid());
    BOOST_CHECK(VerifyOfficialPackagesManifestSignature(signed_json, pubkey));

    // Any change to the signed payload must invalidate the signature.
    std::string tampered = signed_json;
    tampered.replace(tampered.find("prune_mib\": 1907"), 16, "prune_mib\": 1908");
    BOOST_CHECK(!VerifyOfficialPackagesManifestSignature(tampered, pubkey));
}

BOOST_AUTO_TEST_CASE(parse_remote_manifest_requires_signature)
{
    const std::string unsigned_json = R"({
  "packages": [
    {
      "id": "mainnet-910000-prune-2gb",
      "snapshot_height": 910000,
      "base_blockhash": "0000000000000000000108970acb9522ffd516eae17acddcb1bd16469194a821",
      "prune_mib": 1907,
      "download_uri": "https://downloads.bitcoinpurity.org/mainnet/mainnet-910000-prune-2gb.zip",
      "archive_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
  ]
})";

    const auto packages = ParseOfficialDataPackagesFromJson(
        unsigned_json, "test", OfficialPackageTrustPolicy::REMOTE_SIGNED);
    BOOST_CHECK(packages.empty());

    const auto local_packages = ParseOfficialDataPackagesFromJson(
        unsigned_json, "test", OfficialPackageTrustPolicy::LOCAL);
    BOOST_CHECK_EQUAL(local_packages.size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
