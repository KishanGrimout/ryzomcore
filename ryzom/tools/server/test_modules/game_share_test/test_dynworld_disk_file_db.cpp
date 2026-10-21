#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <nel/misc/common.h>
#include <nel/misc/file.h>
#include <nel/misc/path.h>

#include <game_share/dynworld/disk_file_db.h>

namespace {

	// The db implementation hardcodes its directory (relative to the process
	// working directory) and names each record <key> + ".sector"
	std::string fileNameFor(uint64 key)
	{
		return std::string("./cache/map/") + NLMISC::toString(key) + ".sector";
	}

	const uint64 KeyStored = 0x1111111111111111ULL;
	const uint64 KeyStoredBis = 0x2222222222222222ULL;
	const uint64 KeyMissing = 0xef98461237461227ULL;

	std::vector<uint8> makePattern(uint32 size, uint8 seed)
	{
		std::vector<uint8> data(size);
		for (uint32 i = 0; i < size; ++i)
			data[i] = uint8(seed + i * 7);
		return data;
	}

} // anonymous namespace

// Test suite for CDiskFileDataBase (the dynworld disk-backed sector cache
// backend): store/append/read record round trips on real files under
// ./cache/map/, relative to the working directory.
class CUTDynworldDiskFileDbTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// The db does not create the parent directories itself
		NLMISC::CFile::createDirectoryTree("./cache/map/");
		m_DB.reset(new CDiskFileDataBase("unused/ignored/path"));
	}

	void TearDown() override
	{
		m_DB.reset();
		for (uint64 key : m_TrackedKeys)
			NLMISC::CFile::deleteFile(fileNameFor(key));
		m_TrackedKeys.clear();
	}

	CDiskFileDataBase &db() { return *m_DB; }

	void trackKey(uint64 key) { m_TrackedKeys.push_back(key); }

private:
	std::unique_ptr<CDiskFileDataBase> m_DB;
	std::vector<uint64> m_TrackedKeys;
};

TEST_F(CUTDynworldDiskFileDbTest, StoreAndReadRoundTrip)
{
	trackKey(KeyStored);

	std::vector<uint8> data = makePattern(256, 42);
	ASSERT_TRUE(db().storeData(KeyStored, data.data(), (sint64)data.size()));

	std::vector<uint8> readBuffer(data.size(), 0);
	sint64 readLen = (sint64)readBuffer.size();
	ASSERT_TRUE(db().readData(KeyStored, readBuffer.data(), readLen));

	EXPECT_EQ((sint64)data.size(), readLen);
	EXPECT_EQ(0, memcmp(data.data(), readBuffer.data(), data.size()));
}

TEST_F(CUTDynworldDiskFileDbTest, ReadClampsLengthToAvailableBytes)
{
	trackKey(KeyStored);

	std::vector<uint8> data = makePattern(16, 3);
	ASSERT_TRUE(db().storeData(KeyStored, data.data(), (sint64)data.size()));

	// A bigger-than-file read request is clamped down to the stored size
	std::vector<uint8> readBuffer(64, 0);
	sint64 readLen = (sint64)readBuffer.size();
	ASSERT_TRUE(db().readData(KeyStored, readBuffer.data(), readLen));
	EXPECT_EQ((sint64)data.size(), readLen);

	// Reads starting at an offset are clamped to the remaining bytes
	sint64 readLen2 = (sint64)readBuffer.size();
	ASSERT_TRUE(db().readData(KeyStored, readBuffer.data(), readLen2, 8));
	EXPECT_EQ((sint64)(data.size() - 8), readLen2);
	EXPECT_EQ(0, memcmp(data.data() + 8, readBuffer.data(), (size_t)readLen2));
}

TEST_F(CUTDynworldDiskFileDbTest, AppendDataExtendsExistingRecord)
{
	trackKey(KeyStored);

	std::vector<uint8> firstPart = makePattern(16, 1);
	std::vector<uint8> secondPart = makePattern(8, 100);
	ASSERT_TRUE(db().storeData(KeyStored, firstPart.data(), (sint64)firstPart.size()));
	ASSERT_TRUE(db().appendData(KeyStored, secondPart.data(), (sint64)secondPart.size()));

	const sint64 totalSize = (sint64)(firstPart.size() + secondPart.size());
	std::vector<uint8> readBuffer(totalSize + 16, 0);
	sint64 readLen = (sint64)readBuffer.size();
	ASSERT_TRUE(db().readData(KeyStored, readBuffer.data(), readLen));
	EXPECT_EQ(totalSize, readLen);
	EXPECT_EQ(0, memcmp(firstPart.data(), readBuffer.data(), firstPart.size()));
	EXPECT_EQ(0, memcmp(secondPart.data(), readBuffer.data() + firstPart.size(), secondPart.size()));
}

TEST_F(CUTDynworldDiskFileDbTest, StoreDataReplacesPreviousContent)
{
	trackKey(KeyStored);
	trackKey(KeyStoredBis);

	std::vector<uint8> oldData = makePattern(16, 5);
	std::vector<uint8> newData = makePattern(8, 200);
	ASSERT_TRUE(db().storeData(KeyStored, oldData.data(), (sint64)oldData.size()));
	// appendData keeps the record, storeData rewrites it
	ASSERT_TRUE(db().storeData(KeyStoredBis, oldData.data(), (sint64)oldData.size()));
	ASSERT_TRUE(db().storeData(KeyStoredBis, newData.data(), (sint64)newData.size()));

	std::vector<uint8> readBuffer(32, 0);
	sint64 readLen = (sint64)readBuffer.size();
	ASSERT_TRUE(db().readData(KeyStoredBis, readBuffer.data(), readLen));
	EXPECT_EQ((sint64)newData.size(), readLen);
	EXPECT_EQ(0, memcmp(newData.data(), readBuffer.data(), newData.size()));
}

TEST_F(CUTDynworldDiskFileDbTest, ReadMissingKeyReturnsFalse)
{
	std::vector<uint8> readBuffer(16, 0);
	sint64 readLen = (sint64)readBuffer.size();
	EXPECT_FALSE(db().readData(KeyMissing, readBuffer.data(), readLen));
	EXPECT_EQ((sint64)readBuffer.size(), readLen);
}
