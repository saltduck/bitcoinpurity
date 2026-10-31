// Copyright (c) 2019-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>
#include <chain.h>
#include <common/args.h>
#include <flatfile.h>
#include <interfaces/chain.h>
#include <kernel/chain.h>
#include <streams.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(flatfile_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(flatfile_large_positions)
{
    uint64_t large{(uint64_t{1} << 32) + 123};
    FlatFilePos pos(7, large);
    BOOST_CHECK_EQUAL(pos.nPos, large);
    DataStream stream;
    stream << pos;
    FlatFilePos decoded;
    stream >> decoded;
    BOOST_CHECK(decoded == pos);
    BOOST_CHECK_EQUAL(decoded.nPos, large);

    CBlockFileInfo info;
    info.nSize = large;
    info.nUndoSize = large + 456;
    stream << info;
    CBlockFileInfo decoded_info;
    stream >> decoded_info;
    BOOST_CHECK_EQUAL(decoded_info.nSize, large);
    BOOST_CHECK_EQUAL(decoded_info.nUndoSize, large + 456);

    const uint256 hash{};
    CBlockIndex index;
    {
        LOCK(cs_main);
        index.phashBlock = &hash;
        index.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
        index.nFile = 7;
        index.nDataPos = large;
        index.nUndoPos = large + 456;
        stream << CDiskBlockIndex{&index};
    }
    CDiskBlockIndex decoded_index;
    stream >> decoded_index;
    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(decoded_index.GetBlockPos().nPos, large);
        BOOST_CHECK_EQUAL(decoded_index.GetUndoPos().nPos, large + 456);
    }
    BOOST_CHECK_EQUAL(kernel::MakeBlockInfo(&index).data_pos, large);

    // Existing positions must keep their original VARINT encoding.
    for (const uint32_t offset : {0U, 127U, 128U, 0xffffffffU}) {
        DataStream legacy;
        int file{7};
        legacy << VARINT_MODE(file, VarIntMode::NONNEGATIVE_SIGNED) << VARINT(offset);
        DataStream current;
        current << FlatFilePos{file, offset};
        BOOST_CHECK(legacy.str() == current.str());
    }
}

BOOST_AUTO_TEST_CASE(flatfile_large_file)
{
    FlatFileSeq seq{m_args.GetDataDirBase(), "large", 100};
    const auto path{seq.FileName({0, 0})};
    // Sparse writes exercise the 2 GiB and 4 GiB boundaries without filling disk.
    for (const uint64_t offset : {(uint64_t{1} << 31) + 123, (uint64_t{1} << 32) + 123}) {
        {
            AutoFile file{seq.Open(FlatFilePos(0, offset))};
            BOOST_REQUIRE(!file.IsNull());
            BOOST_CHECK_EQUAL(file.tell(), offset);
            file << uint32_t{0x12345678};
        }
        BOOST_CHECK_EQUAL(fs::file_size(path), offset + sizeof(uint32_t));
        {
            AutoFile file{seq.Open(FlatFilePos(0, offset), true)};
            uint32_t value;
            file >> value;
            BOOST_CHECK_EQUAL(value, 0x12345678U);
            file.seek(-4, SEEK_END);
            BOOST_CHECK_EQUAL(file.tell(), offset);
        }
        BOOST_REQUIRE(seq.Flush(FlatFilePos(0, offset + 2), true));
        BOOST_CHECK_EQUAL(fs::file_size(path), offset + 2);
        {
            AutoFile file{seq.Open({0, 0})};
            BOOST_REQUIRE(file.Truncate(offset + 1));
        }
        BOOST_CHECK_EQUAL(fs::file_size(path), offset + 1);
        const uint64_t allocation_start{((offset + 99) / 100) * 100};
        BOOST_REQUIRE(seq.Flush(FlatFilePos(0, allocation_start), true));
        bool out_of_space{false};
        BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, allocation_start), 1, out_of_space), 100U);
        BOOST_CHECK(!out_of_space);
        BOOST_CHECK_EQUAL(fs::file_size(path), allocation_start + 100);
    }
}

BOOST_AUTO_TEST_CASE(flatfile_filename)
{
    const auto data_dir = m_args.GetDataDirBase();

    FlatFilePos pos(456, 789);

    FlatFileSeq seq1(data_dir, "a", 16 * 1024);
    BOOST_CHECK_EQUAL(seq1.FileName(pos), data_dir / "a00456.dat");

    FlatFileSeq seq2(data_dir / "a", "b", 16 * 1024);
    BOOST_CHECK_EQUAL(seq2.FileName(pos), data_dir / "a" / "b00456.dat");

    // Check default constructor IsNull
    assert(FlatFilePos{}.IsNull());
}

BOOST_AUTO_TEST_CASE(flatfile_open)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 16 * 1024);

    std::string line1("A purely peer-to-peer version of electronic cash would allow online "
                      "payments to be sent directly from one party to another without going "
                      "through a financial institution.");
    std::string line2("Digital signatures provide part of the solution, but the main benefits are "
                      "lost if a trusted third party is still required to prevent double-spending.");

    size_t pos1 = 0;
    size_t pos2 = pos1 + GetSerializeSize(line1);

    // Write first line to file.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos1))};
        file << LIMITED_STRING(line1, 256);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }

    // Attempt to append to file opened in read-only mode.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos2), true)};
        BOOST_CHECK_THROW(file << LIMITED_STRING(line2, 256), std::ios_base::failure);
    }

    // Append second line to file.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos2))};
        file << LIMITED_STRING(line2, 256);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }

    // Read text from file in read-only mode.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(0, pos1), true)};

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line1);

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line2);
    }

    // Read text from file with position offset.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(0, pos2))};

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line2);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }

    // Ensure another file in the sequence has no data.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(1, pos2))};
        BOOST_CHECK_THROW(file >> LIMITED_STRING(text, 256), std::ios_base::failure);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }
}

BOOST_AUTO_TEST_CASE(flatfile_allocate)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 100);

    bool out_of_space;

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 0), 1, out_of_space), 100U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 0))), 100U);
    BOOST_CHECK(!out_of_space);

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 99), 1, out_of_space), 0U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 99))), 100U);
    BOOST_CHECK(!out_of_space);

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 99), 2, out_of_space), 101U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 99))), 200U);
    BOOST_CHECK(!out_of_space);
}

BOOST_AUTO_TEST_CASE(flatfile_flush)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 100);

    bool out_of_space;
    seq.Allocate(FlatFilePos(0, 0), 1, out_of_space);

    // Flush without finalize should not truncate file.
    seq.Flush(FlatFilePos(0, 1));
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 1))), 100U);

    // Flush with finalize should truncate file.
    seq.Flush(FlatFilePos(0, 1), true);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 1))), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
