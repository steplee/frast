#include "run.h"

namespace frast {

FlatEnvironment::FlatEnvironment(const std::string& path, const EnvOptions& opts)
		: BaseEnvironment<FlatEnvironment>(path, opts), path_(path)
{

		// basePointer = static_cast<void*>(static_cast<char*>(basePointer) + 4096);
		fmt::print(" - sizeof(FileMeta) = {}\n", sizeof(FileMeta));

		if (fileIsNew_ or opts.anon) {
			int r = fallocate(fd_, 0,0,4096);
			if (r != 0) throw std::runtime_error("fallocate failed: " + std::string{strerror(errno)});

			new (meta()) FileMeta;
			currentEnd = fileMetaCapacity;
			// *meta() = FileMeta;
			// fileMetaLength =
			// metaOffset = metaLength =
			// keysOffset = keysLength =
			// valsOffset = valsLength = 0;
			fmt::print(" - [FlatEnv] using new file, currentEnd {}\n", currentEnd);
		} else {

			// uint64_t* baseAsULong = static_cast<uint64_t*>(basePointer);
			// fileMetaLength = baseAsULong[0];
			// metaOffset = baseAsULong[1];
			// metaLength = baseAsULong[2];
			// keysOffset = baseAsULong[3];
			// keysLength = baseAsULong[4];
			// valsOffset = baseAsULong[5];
			// valsLength = baseAsULong[6];
			uint64_t off = fileMetaCapacity;
			for (int i=0; i<26; i++) {
				uint64_t off_ = meta()->levelSpecs[i].valsOffset + meta()->levelSpecs[i].valsLength;
				while (off_ % 4096 != 0) off_++;
				off = std::max(off,off_);
			}
			currentEnd = off;
			fmt::print(" - [FlatEnv] using old file, found currentEnd {}\n", currentEnd);

		}

}
FlatEnvironment::~FlatEnvironment() {
		// fmt::print(" - [~FlatEnv] writing  {}\n", head);
		// static_cast<uint64_t*>(basePointer)[0] = head;
		// fmt::print(" - [~ArenaEnv] wrote   head {}\n", static_cast<uint64_t*>(basePointer)[0]);
}

bool FlatEnvironment::beginLevel(int lvl) {
	assert(meta()->levelSpecs[lvl].keysCapacity == 0 && "this level should be empty");

	if (currentEnd % 4096 != 0) {
		fmt::print(" - Warning: currentEnd was not at a multiple of block size, adding needed padding.\n");
		while (currentEnd % 4096 != 0) currentEnd++;
	}

	auto& spec = meta()->levelSpecs[lvl];

	// static constexpr uint64_t iniNumKeys = 4096;
	// static constexpr uint64_t iniValBlobSize = 1024*4096;
	static constexpr uint64_t iniNumKeys = 512;
	static constexpr uint64_t iniValBlobSize = 4096;

	// Allocate space for keys & k2vs
	// NOTE: Each should be divisible by the fallocate block size (which i think is typicall 4096)
	spec.keysOffset = currentEnd;
	spec.keysCapacity = sizeof(uint64_t) * iniNumKeys;
	currentEnd += spec.keysCapacity;

	spec.k2vsOffset = currentEnd;
	currentEnd += spec.keysCapacity;

	spec.valsOffset = currentEnd;
	spec.valsCapacity = iniValBlobSize;
	currentEnd += spec.valsCapacity;

	fmt::print(" - [beginLevel] lvl={} ko={}, k2vo={}, vo={}\n", lvl, spec.keysOffset, spec.k2vsOffset, spec.valsOffset);
	int r = fallocate(fd_, 0, 0, currentEnd);
	if (r != 0) {
		throw std::runtime_error("fallocate() failed: " + std::string{strerror(errno)});
	}

	currentLvl = lvl;

	return false;
}

static std::string byteSizeToString(uint64_t x) {
	if (x < 1<<10)
		return fmt::format("{}B", x);
	if (x < 1<<20)
		return fmt::format("{:.1f}KB", static_cast<float>(x) / (1<<10));
	if (x < 1<<30)
		return fmt::format("{:.1f}MB", static_cast<float>(x) / (1<<20));
	return fmt::format("{:.1f}GB", static_cast<float>(x) / (1<<30));
}

void FlatEnvironment::printSomeInfo() {
	int nresident = 0;
	for (int i=0; i<26; i++) {
		if (meta()->levelSpecs[i].keysLength != 0) nresident++;
	}
	fmt::print(" - File '{}':\n", path_);
	fmt::print("          - {} Levels:\n", nresident);
	uint64_t totalUsedSpace = 0, totalTakenSpace = 0;
	for (int i=0; i<26; i++) {
		if (meta()->levelSpecs[i].keysLength != 0) {
				uint64_t nitems  = meta()->levelSpecs[i].keysLength / 8;
				auto valSize = byteSizeToString(meta()->levelSpecs[i].valsLength);
				auto totSize = byteSizeToString(2*sizeof(uint64_t)*nitems + meta()->levelSpecs[i].valsLength);
				float usagek = static_cast<float>(meta()->levelSpecs[i].keysLength) / static_cast<float>(meta()->levelSpecs[i].keysCapacity);
				float usagev = static_cast<float>(meta()->levelSpecs[i].valsLength) / static_cast<float>(meta()->levelSpecs[i].valsCapacity);
				totalUsedSpace  += 2*sizeof(uint64_t)*nitems + meta()->levelSpecs[i].valsLength;
				totalTakenSpace += 2*meta()->levelSpecs[i].keysCapacity + meta()->levelSpecs[i].valsCapacity;
				fmt::print("                 - Level {:>2d}: {} items, {} valSize, {} total used size, usage: {:2.1f}k {:2.1f}v\n", i, nitems, valSize, totSize, usagek, usagev);
		}
	}
	float usageRatio = static_cast<float>(totalUsedSpace) / static_cast<float>(totalTakenSpace);
	fmt::print("          - {} used\n", byteSizeToString(totalUsedSpace));
	fmt::print("          - {} allocated ({:2.1f} usage)\n", byteSizeToString(totalTakenSpace), usageRatio);
}
void FlatEnvironment::printFirstLastEightCurLvl() {
	auto& spec = meta()->levelSpecs[currentLvl];
	using namespace fmt;
	print(" - first 8 keys:");
	for (int i=0; i<8; i++)
		print(" {}", reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + spec.keysOffset)[i]);
	print("\n");
	print(" - first 8 k2vs:");
	for (int i=0; i<8; i++)
		print(" {}", reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + spec.k2vsOffset)[i]);
	print("\n");
	print(" - first 8 firstBytesOfVals:");
	for (int i=0; i<8; i++) {
		uint64_t local_v_offset = reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + spec.k2vsOffset)[i];
		uint8_t byte = reinterpret_cast<uint8_t*>(static_cast<char*>(basePointer) + spec.valsOffset + local_v_offset)[0];
		print(" {}", byte);
	}
	print("\n");
	print(" - last  8 keys:");
	for (int i=spec.keysCapacity/8-8; i<spec.keysCapacity/8; i++)
		print(" {}", reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + spec.keysOffset)[i]);
	print("\n");
	print(" - last  8 k2vs:");
	for (int i=spec.keysCapacity/8-8; i<spec.keysCapacity/8; i++)
		print(" {}", reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + spec.k2vsOffset)[i]);
	print("\n");
	print(" - last  8 firstBytesOfVals:");
	for (int i=spec.keysCapacity/8-8; i<spec.keysCapacity/8; i++) {
		uint64_t local_v_offset = reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + spec.k2vsOffset)[i];
		uint8_t byte = reinterpret_cast<uint8_t*>(static_cast<char*>(basePointer) + spec.valsOffset + local_v_offset)[0];
		print(" {}", byte);
	}
	print("\n");
}


uint64_t FlatEnvironment::growLevelKeys() {
	assert(currentLvl != 0);
	auto& spec = meta()->levelSpecs[currentLvl];

	fmt::print(" - [growLevelKeys] lvl={}, from ko={}, k2vo={}, vo={}, kcap={}, vcap={}\n",
				currentLvl, spec.keysOffset, spec.k2vsOffset, spec.valsOffset, spec.keysCapacity, spec.keysOffset);
	// printFirstLastEightCurLvl();


	// Change is 2x because we grow keys & k2vs
	uint64_t oldCap = spec.keysCapacity;
	uint64_t g = spec.keysCapacity * 2;
	uint64_t k2vs_change = g - oldCap;
	uint64_t vals_change = (g - oldCap) * 2;

	uint64_t oldK2vsOffset = spec.k2vsOffset;

	// fmt::print(" - (pre ) distance b/t end of k2vs and start of values: {}\n", spec.valsOffset-(spec.k2vsOffset+spec.keysCapacity));

	int r = fallocate(fd_, FALLOC_FL_INSERT_RANGE, oldK2vsOffset+oldCap, vals_change);
	if (r != 0) throw std::runtime_error("fallocate failed: " + std::string{strerror(errno)});

	spec.keysCapacity = g;
	spec.k2vsOffset += k2vs_change;
	spec.valsOffset += vals_change;
	// fmt::print(" - vals offset change: {}, ({} -> {})\n", vals_change, spec.valsOffset-vals_change, spec.valsOffset);

	currentEnd = spec.valsOffset + spec.valsCapacity;

	// We have to copy the k2vs into there new places.
	// NOTE: Do it backwards in-case there is overlap.
	for (int64_t i=oldCap/8-1; i>=0; i--) {
		uint64_t x = reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + oldK2vsOffset)[i];
		reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + spec.k2vsOffset)[i] = x;
	}
	// NOTE: No real need to do this, but do it to make tests look nicer
	bzero(((char*)basePointer)+oldK2vsOffset, oldCap);

	fmt::print(" - [growLevelKeys] lvl={}, to   ko={}, k2vo={}, vo={}, kcap={}, vcap={}\n",
				currentLvl, spec.keysOffset, spec.k2vsOffset, spec.valsOffset, spec.keysCapacity, spec.keysOffset);
	// fmt::print(" - (post) distance b/t end of k2vs and start of values: {}\n", spec.valsOffset-(spec.k2vsOffset+spec.keysCapacity));
	// printFirstLastEightCurLvl();

	return g;
}
uint64_t FlatEnvironment::growLevelValues() {
	assert(currentLvl != 0);
	auto& spec = meta()->levelSpecs[currentLvl];

	// Here, we need not do any updating of any offsets
	uint64_t oldValsOffset = spec.valsOffset;
	uint64_t oldCap = spec.valsCapacity;
	uint64_t g = spec.valsCapacity * 2;
	uint64_t vals_change = (g - oldCap);
	spec.valsCapacity += vals_change;
	fmt::print(" - [growLevelVals] lvl={}, increasing valsCap to {} from {} (+{})\n", currentLvl, spec.valsCapacity, oldCap, vals_change);

	// We cannot use the 'insert range' mode when we want to extend the file at the end,
	// so use the default fallocate mode.
	if (currentEnd == oldValsOffset+oldCap) {
		int r = fallocate(fd_, 0, 0, spec.valsOffset+spec.valsCapacity);
		if (r != 0) throw std::runtime_error("fallocate failed: " + std::string{strerror(errno)});
	} else {
		// NOTE: This is not supported rn because we can only alter the current level.
		assert(false);
		int r = fallocate(fd_, FALLOC_FL_INSERT_RANGE, oldValsOffset+oldCap, vals_change);
		if (r != 0) throw std::runtime_error("fallocate failed: " + std::string{strerror(errno)});
	}

	currentEnd = spec.valsOffset + spec.valsCapacity;
	return g;
}


}


/*
int main() {

	using namespace frast;
	EnvOptions opts;
	FlatEnvironment e("test.it", opts);

	e.beginLevel(5);
	// e.beginLevel(6);

	e.meta()->levelSpecs[5].keysLength = e.meta()->levelSpecs[5].keysCapacity;
	e.meta()->levelSpecs[5].valsLength = e.meta()->levelSpecs[5].valsCapacity;
	for (int i=0; i<e.meta()->levelSpecs[5].valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
	for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getKeys(5)[i] = i;
	for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getK2vs(5)[i] = i;

	e.growLevelKeys();
	e.growLevelKeys();

	for (int i=0; i<e.meta()->levelSpecs[5].valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
	for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getKeys(5)[i] = i;
	for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getK2vs(5)[i] = i;

	e.growLevelKeys();
	e.printSomeInfo();

	return 0;
}
*/
