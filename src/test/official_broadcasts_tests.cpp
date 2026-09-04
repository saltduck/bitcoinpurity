// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/official_broadcasts.h>

#include <key.h>
#include <kernel/official_packages.h>
#include <util/strencodings.h>

#include <univalue.h>

#include <string>
#include <vector>

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
  "notices": [
    {
      "id": "maintenance-2026-09",
      "title": "Scheduled maintenance",
      "body": "CDN maintenance is planned this weekend.",
      "published_at": "2026-09-01T00:00:00Z",
      "expires_at": "2026-12-01T00:00:00Z"
    },
    {
      "id": "welcome",
      "body": "Welcome to Bitcoin Purity.",
      "published_at": "2026-01-01T00:00:00Z"
    }
  ]
})";

} // namespace

BOOST_FIXTURE_TEST_SUITE(official_broadcasts_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(parse_signed_manifest)
{
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();

    const std::string signed_json = AddManifestSignature(SAMPLE_UNSIGNED_MANIFEST, SignManifest(key, SAMPLE_UNSIGNED_MANIFEST));
    const auto notices = ParseOfficialBroadcastsManifest(
        signed_json, "test", OfficialBroadcastTrustPolicy::LOCAL);
    BOOST_REQUIRE(notices);
    BOOST_REQUIRE_EQUAL(notices->size(), 2U);
    BOOST_CHECK_EQUAL((*notices)[0].id, "maintenance-2026-09");
    BOOST_CHECK_EQUAL((*notices)[0].title, "Scheduled maintenance");
    BOOST_CHECK_EQUAL((*notices)[0].body, "CDN maintenance is planned this weekend.");
    BOOST_CHECK_EQUAL((*notices)[1].id, "welcome");
    BOOST_CHECK((*notices)[1].title.empty());
    BOOST_CHECK(VerifyOfficialPackagesManifestSignature(signed_json, pubkey));
}

BOOST_AUTO_TEST_CASE(reject_unsigned_remote_manifest)
{
    const auto notices = ParseOfficialBroadcastsManifest(
        SAMPLE_UNSIGNED_MANIFEST, "test", OfficialBroadcastTrustPolicy::REMOTE_SIGNED);
    BOOST_CHECK(!notices);
}

BOOST_AUTO_TEST_CASE(reject_bad_schema)
{
    const std::string bad = R"({"schema": 99, "notices": []})";
    const auto notices = ParseOfficialBroadcastsManifest(
        bad, "test", OfficialBroadcastTrustPolicy::LOCAL);
    BOOST_CHECK(!notices);
}

BOOST_AUTO_TEST_CASE(skip_invalid_notices)
{
    const std::string json = R"({
      "schema": 1,
      "notices": [
        {"id": "no-body"},
        {"body": "missing id"},
        {"id": "ok", "body": "hello"},
        {"id": "ok", "body": "duplicate id ignored"},
        {"id": "bad-expiry", "body": "x", "expires_at": "not-a-date"}
      ]
    })";
    const auto notices = ParseOfficialBroadcastsManifest(
        json, "test", OfficialBroadcastTrustPolicy::LOCAL);
    BOOST_REQUIRE(notices);
    BOOST_REQUIRE_EQUAL(notices->size(), 1U);
    BOOST_CHECK_EQUAL((*notices)[0].id, "ok");
}

BOOST_AUTO_TEST_CASE(filter_unread_and_expired)
{
    std::vector<OfficialBroadcastNotice> notices{
        {"a", "A", "body a", "2026-01-01T00:00:00Z", ""},
        {"b", "B", "body b", "2026-01-02T00:00:00Z", "2026-06-01T00:00:00Z"},
        {"c", "C", "body c", "2026-01-03T00:00:00Z", "2026-12-01T00:00:00Z"},
    };

    const auto unread = FilterUnreadOfficialBroadcasts(
        notices, {"a"}, "2026-09-01T00:00:00Z");
    BOOST_REQUIRE_EQUAL(unread.size(), 1U);
    BOOST_CHECK_EQUAL(unread[0].id, "c");
}

BOOST_AUTO_TEST_CASE(default_url)
{
    BOOST_CHECK_EQUAL(
        GetDefaultOfficialBroadcastsManifestUrl(),
        "https://downloads.bitcoinpurity.org/broadcasts.json");
}

BOOST_AUTO_TEST_SUITE_END()
