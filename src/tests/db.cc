#include <gtest/gtest.h>
#include <unistd.h>
#include <algorithm>

#include "../db.h"

namespace {
	const std::string TMP_DIR = "/tmp/";
	void eraseFile(const std::string& path) {
		unlink(path.c_str());
	}
}

class DatasetReaderTester : public ::testing::Test {
	public:
		void test_findBestLvlAndTlbr_dataDependent();
};

TEST_F(DatasetReaderTester, BasicTests) {
  // Expect two strings not to be equal.
  EXPECT_STRNE("hello", "world");
  // Expect equality.
  EXPECT_EQ(7 * 6, 42);
}

TEST(DatasetBasic, addAndDropLevel) {
	std::string path = TMP_DIR + "tmpDataset1.ft";

	DatasetWritable dset(path, {});

	// New dataset should have no levels
	for (int i=0; i<MAX_LVLS; i++)
		EXPECT_FALSE(dset.hasLevel(i));

	// Add some levels
	std::vector<int> levelsToCreate = {2,4,7,9,15,MAX_LVLS-1};
	for (auto lvl : levelsToCreate)
		dset.createLevelIfNeeded(lvl);

	// Dataset should only have those
	for (int i=0; i<MAX_LVLS; i++) {
		if (std::find(levelsToCreate.begin(), levelsToCreate.end(), i) != levelsToCreate.end()) {
			EXPECT_TRUE(dset.hasLevel(i));
		} else {
			EXPECT_FALSE(dset.hasLevel(i));
		}
	}

	// Drop some
	MDB_txn* txn;
	dset.beginTxn(&txn);
	for (int n=0; n<3; n++) {
		assert(levelsToCreate.size()); // normal assert: assert on the actual test code
		dset.dropLvl(levelsToCreate.back(), txn);
		levelsToCreate.pop_back();
	}
	dset.endTxn(&txn);

	// Dataset should have dropped those ones
	for (int i=0; i<MAX_LVLS; i++) {
		if (std::find(levelsToCreate.begin(), levelsToCreate.end(), i) != levelsToCreate.end()) {
			EXPECT_TRUE(dset.hasLevel(i));
		} else {
			EXPECT_FALSE(dset.hasLevel(i));
		}
	}

	eraseFile(path);
	eraseFile(path + "-lock");
}
