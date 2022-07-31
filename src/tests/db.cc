#include <gtest/gtest.h>
#include <unistd.h>
#include <algorithm>

#include "../db.h"

namespace {
const std::string TMP_DIR = "/tmp/";

// false on failure. But succeeed if ENOENT
bool eraseFile(const std::string &path) {
	auto err = unlink(path.c_str());
	if (not err) return false;
	if (err and errno == ENOENT) return false;
	return true;
}

void write_some_tiles(const std::string &path, const std::vector<BlockCoordinate> coords[MAX_LVLS]) {
	DatasetWritable dset(path, {});

	dset.configure(1, 128);

	dset.setTileSize(256);
	dset.setFormat((int32_t)Image::Format::GRAY);

	for (int i=0; i<MAX_LVLS; i++)
		if (coords[i].size())
			dset.createLevelIfNeeded(i);

	std::vector<uint8_t> fake_data(128);
	MDB_val fake_val;
	fake_val.mv_size = fake_data.size();
	fake_val.mv_data = fake_data.data();

	for (int i=0; i<MAX_LVLS; i++) {
		if (coords[i].size()) {

			dset.sendCommand(DbCommand{DbCommand::BeginLvl, (int32_t) i});
			dset.blockUntilEmptiedQueue();

			for (int j=0; j<coords[i].size(); j++) {
				WritableTile& tile = dset.blockingGetTileBufferForThread(0);
				tile.fillWith(coords[i][j], fake_val);
				dset.sendCommand({DbCommand::TileReady, tile.bufferIdx});
			}

			dset.sendCommand(DbCommand{DbCommand::EndLvl, (int32_t) i});
			dset.blockUntilEmptiedQueue();

		}
	}

	MDB_txn *txn;
	dset.beginTxn(&txn);
	dset.recompute_meta_and_write_slow(txn);
	dset.endTxn(&txn);
}

}  // namespace

// This is bad design. But functional
class DatasetReaderTester : public ::testing::Test {
  public:
	inline void test_findBestLvlAndTlbr_dataDependent() {
		std::string path = TMP_DIR + "tmpDataset2.ft";


		// Create tiles on level zero and on level 16
		std::vector<BlockCoordinate> coordss[MAX_LVLS];
		uint16_t tlbr_on_lvl_16[4] = {10,10, 14,14};

		{
			coordss[0].push_back(BlockCoordinate{0,0,0});

			for (uint64_t y=tlbr_on_lvl_16[1]; y<tlbr_on_lvl_16[3]; y++)
				for (uint64_t x=tlbr_on_lvl_16[0]; x<tlbr_on_lvl_16[2]; x++)
					coordss[16].push_back(BlockCoordinate{16, y,x});

			// Create a fake dataset with some zero-filled (but existent) tiles
			write_some_tiles(path, coordss);
		}

		DatasetReader dset(path, {});

		MDB_txn *txn;
		dset.beginTxn(&txn, true);
		uint64_t selectedTlbr[4];
		uint64_t selectedLvl;

		// Test that querying for the entire earth into a 256x256 output
		// will select level zero.
		{
			double bbox[4] = {
				-WebMercatorMapScale + 1,
				-WebMercatorMapScale + 1,
				WebMercatorMapScale - 1,
				WebMercatorMapScale - 1
			};
			selectedLvl = dset.findBestLvlAndTlbr_dataDependent(selectedTlbr, 4, 256, 256, bbox, txn);
			EXPECT_EQ(selectedLvl, 0);
		}

		dset.endTxn(&txn);
		eraseFile(path);
		eraseFile(path + "-lock");
	}
};

TEST_F(DatasetReaderTester, FindBestLvlAndTlbrDataDependent) { test_findBestLvlAndTlbr_dataDependent(); }

TEST(DatasetWritable, addAndDropLevel) {
	std::string path = TMP_DIR + "tmpDataset1.ft";

	DatasetWritable dset(path, {});

	// New dataset should have no levels
	for (int i = 0; i < MAX_LVLS; i++) EXPECT_FALSE(dset.hasLevel(i));

	// Add some levels
	std::vector<int> levelsToCreate = {2, 4, 7, 9, 15, MAX_LVLS - 1};
	for (auto lvl : levelsToCreate) dset.createLevelIfNeeded(lvl);

	// Dataset should only have those
	for (int i = 0; i < MAX_LVLS; i++) {
		if (std::find(levelsToCreate.begin(), levelsToCreate.end(), i) != levelsToCreate.end()) {
			EXPECT_TRUE(dset.hasLevel(i));
		} else {
			EXPECT_FALSE(dset.hasLevel(i));
		}
	}

	// Drop some
	MDB_txn *txn;
	dset.beginTxn(&txn);
	for (int n = 0; n < 3; n++) {
		assert(levelsToCreate.size());	// normal assert: assert on the actual test code
		dset.dropLvl(levelsToCreate.back(), txn);
		levelsToCreate.pop_back();
	}
	dset.endTxn(&txn);

	// Dataset should have dropped those ones
	for (int i = 0; i < MAX_LVLS; i++) {
		if (std::find(levelsToCreate.begin(), levelsToCreate.end(), i) != levelsToCreate.end()) {
			EXPECT_TRUE(dset.hasLevel(i));
		} else {
			EXPECT_FALSE(dset.hasLevel(i));
		}
	}

	eraseFile(path);
	eraseFile(path + "-lock");
}
