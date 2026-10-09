// Copyright (c) 2012-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <clientversion.h>
#include <hash.h>
#include <key.h>
#include <streams.h>
#include <test/util/logging.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>

#include <boost/test/unit_test.hpp>

#include <array>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(walletdb_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(walletdb_readkeyvalue)
{
    /**
     * When ReadKeyValue() reads from either a "key" or "wkey" it first reads the DataStream into a
     * CPrivKey or CWalletKey respectively and then reads a hash of the pubkey and privkey into a uint256.
     * Wallets from 0.8 or before do not store the pubkey/privkey hash, trying to read the hash from old
     * wallets throws an exception, for backwards compatibility this read is wrapped in a try block to
     * silently fail. The test here makes sure the type of exception thrown from DataStream::read()
     * matches the type we expect, otherwise we need to update the "key"/"wkey" exception type caught.
     */
    DataStream ssValue{};
    uint256 dummy;
    BOOST_CHECK_THROW(ssValue >> dummy, std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(walletdb_keypair_hash)
{
    for (const bool compressed : {false, true}) {
        for (const unsigned char seed : {1, 2, 254}) {
            std::array<unsigned char, 32> secret;
            secret.fill(seed);
            CKey key;
            key.Set(secret.begin(), secret.end(), compressed);
            BOOST_REQUIRE(key.IsValid());
            const CPubKey pubkey = key.GetPubKey();
            const CPrivKey privkey = key.GetPrivKey();
            const CPubKey original_pubkey = pubkey;
            const CPrivKey original_privkey = privkey;
            std::vector<unsigned char> concatenated(pubkey.begin(), pubkey.end());
            concatenated.insert(concatenated.end(), privkey.begin(), privkey.end());
            const uint256 old_hash = Hash(concatenated);
            BOOST_CHECK(Hash(pubkey, privkey) == old_hash);
            BOOST_CHECK(pubkey == original_pubkey);
            BOOST_CHECK(privkey == original_privkey);

            auto database = CreateMockableWalletDatabase();
            WalletBatch batch(*database);
            BOOST_REQUIRE(batch.WriteKey(pubkey, privkey, CKeyMetadata{}));
            BOOST_CHECK(!batch.WriteKey(pubkey, privkey, CKeyMetadata{}));
            BOOST_REQUIRE(batch.WriteDescriptorKey(uint256::ONE, pubkey, privkey));
            BOOST_CHECK(!batch.WriteDescriptorKey(uint256::ONE, pubkey, privkey));
            auto records = database->MakeBatch();
            std::pair<CPrivKey, uint256> value;
            BOOST_REQUIRE(records->Read(std::make_pair(DBKeys::KEY, pubkey), value));
            BOOST_CHECK(value.first == privkey);
            BOOST_CHECK(value.second == old_hash);
            BOOST_REQUIRE(records->Read(std::make_pair(DBKeys::WALLETDESCRIPTORKEY, std::make_pair(uint256::ONE, pubkey)), value));
            BOOST_CHECK(value.first == privkey);
            BOOST_CHECK(value.second == old_hash);
        }
    }
}

BOOST_AUTO_TEST_CASE(walletdb_legacy_key_checksums)
{
    for (const bool compressed : {false, true}) {
        CKey key;
        key.MakeNewKey(compressed);
        const CPubKey pubkey = key.GetPubKey();
        const CPrivKey privkey = key.GetPrivKey();
        std::vector<unsigned char> concatenated(pubkey.begin(), pubkey.end());
        concatenated.insert(concatenated.end(), privkey.begin(), privkey.end());
        const uint256 old_hash = Hash(concatenated);

        for (const int record_type : {0, 1, 2, 3, 4, 5}) {
            CWallet wallet(m_node.chain.get(), "", CreateMockableWalletDatabase());
            DataStream db_key{}, db_value{};
            db_key << (record_type == 4 ? CPubKey{} : pubkey);
            const CPrivKey stored_privkey = record_type == 5 ? CPrivKey{1, 2, 3} : privkey;
            db_value << stored_privkey;
            // Exercise pre-0.8 records, null checksums, old checksums, and corruption.
            if (record_type == 1) db_value << uint256{};
            if (record_type == 2 || record_type == 4) db_value << old_hash;
            if (record_type == 3) db_value << uint256::ONE;
            if (record_type == 5) db_value << Hash(pubkey, stored_privkey);
            std::string error;
            const bool loaded = LoadKey(&wallet, db_key, db_value, error);
            if (record_type < 3) {
                BOOST_REQUIRE(loaded);
                BOOST_CHECK(error.empty());
                CKey loaded_key;
                BOOST_REQUIRE(wallet.GetOrCreateLegacyDataSPKM()->GetKey(pubkey.GetID(), loaded_key));
                std::vector<unsigned char> signature;
                BOOST_REQUIRE(loaded_key.Sign(old_hash, signature));
                BOOST_CHECK(pubkey.Verify(old_hash, signature));
            } else {
                BOOST_CHECK(!loaded);
                BOOST_CHECK_EQUAL(error, record_type == 3 ? "Error reading wallet database: CPubKey/CPrivKey corrupt" :
                                         record_type == 4 ? "Error reading wallet database: CPubKey corrupt" :
                                                            "Error reading wallet database: CPrivKey corrupt");
            }
        }
    }
}

BOOST_FIXTURE_TEST_CASE(walletdb_descriptor_key_checksums, TestingSetup)
{
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();
    const CPrivKey privkey = key.GetPrivKey();
    std::vector<unsigned char> concatenated(pubkey.begin(), pubkey.end());
    concatenated.insert(concatenated.end(), privkey.begin(), privkey.end());
    const uint256 old_hash = Hash(concatenated);
    CWallet original(m_node.chain.get(), "", CreateMockableWalletDatabase());
    const uint256 id = CreateDescriptor(original, "pkh(" + HexStr(pubkey) + ")", true)->GetID();

    for (const int record_type : {0, 1, 2, 3}) {
        auto database = DuplicateMockDatabase(original.GetDatabase());
        const auto db_key = std::make_pair(DBKeys::WALLETDESCRIPTORKEY, std::make_pair(id, record_type == 2 ? CPubKey{} : pubkey));
        const CPrivKey stored_privkey = record_type == 3 ? CPrivKey{1, 2, 3} : privkey;
        const auto db_value = std::make_pair(stored_privkey, record_type == 1 ? uint256::ONE :
                                                         record_type == 3 ? Hash(pubkey, stored_privkey) : old_hash);
        BOOST_REQUIRE(database->MakeBatch()->Write(db_key, db_value));
        CWallet wallet(m_node.chain.get(), "", std::move(database));
        if (record_type == 0) {
            BOOST_REQUIRE_EQUAL(wallet.LoadWallet(), DBErrors::LOAD_OK);
            auto loaded_key = wallet.GetKey(pubkey.GetID());
            BOOST_REQUIRE(loaded_key);
            std::vector<unsigned char> signature;
            BOOST_REQUIRE(loaded_key->Sign(old_hash, signature));
            BOOST_CHECK(pubkey.Verify(old_hash, signature));
            std::pair<CPrivKey, uint256> stored;
            BOOST_REQUIRE(wallet.GetDatabase().MakeBatch()->Read(db_key, stored));
            BOOST_CHECK(stored == db_value);
        } else {
            bool logged = false;
            const std::string error = record_type == 1 ? "Error reading wallet database: descriptor unencrypted key CPubKey/CPrivKey corrupt" :
                                      record_type == 2 ? "Error reading wallet database: descriptor unencrypted key CPubKey corrupt" :
                                                         "Error reading wallet database: descriptor unencrypted key CPrivKey corrupt";
            DebugLogHelper log_helper(error, [&](const std::string*) { logged = true; return false; });
            BOOST_CHECK_EQUAL(wallet.LoadWallet(), DBErrors::CORRUPT);
            BOOST_CHECK(logged);
        }
    }
}

BOOST_AUTO_TEST_CASE(walletdb_read_write_deadlock)
{
    // Exercises a db read write operation that shouldn't deadlock.
    for (const DatabaseFormat& db_format : DATABASE_FORMATS) {
        // Context setup
        DatabaseOptions options;
        options.require_format = db_format;
        DatabaseStatus status;
        bilingual_str error_string;
        std::unique_ptr<WalletDatabase> db = MakeDatabase(m_path_root / strprintf("wallet_%d_.dat", db_format).c_str(), options, status, error_string);
        BOOST_CHECK_EQUAL(status, DatabaseStatus::SUCCESS);

        std::shared_ptr<CWallet> wallet(new CWallet(m_node.chain.get(), "", std::move(db)));
        wallet->m_keypool_size = 4;

        // Create legacy spkm
        LOCK(wallet->cs_wallet);
        auto legacy_spkm = wallet->GetOrCreateLegacyScriptPubKeyMan();
        BOOST_CHECK(legacy_spkm->SetupGeneration(true));
        wallet->Flush();

        // Now delete all records, which performs a read write operation.
        BOOST_CHECK(wallet->GetLegacyScriptPubKeyMan()->DeleteRecords());
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
