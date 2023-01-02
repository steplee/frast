#pragma once

#include "detail/env.h"
#include "coordinates.h"

#include <mutex>
#include <cassert>
#include <vector>


namespace frast {

struct Value {
	void* value;
	uint32_t len;
};

// The structure of the file is:
//       [1] FileMeta (exactly 1 4096 block)
//       [2] Level_A (keys, followed by values)
//       [3] Level_B (keys, followed by values)
//           ...
//
// FileMeta holds 26 LevelSpecs. Each LevelSpec tells where the contigous keys and values
// Keys and Values are contiguous. When we search the keys, we can do so with a binary search.
// To then get the value, we need the offset into the values blob. We store this in an adjacent array called k2vs,
// which is indexed just like the keys.
//
// Running with an unknown number of items:
// A new level is appended at the end of the file.
// We don't know how much space to allocate for the keys and values,
// and we want to avoid linked lists and trees and just have a contiguous blob.
// To achieve this, the keys and k2vs arrays can be expanded with fallocate(), which
// then shifts the values to the right. We just need to update this level's valsOffset,
// and because I treat keys/k2vs as two planar arrays (but fallocate once), we need
// also to copy the k2vs to the new offset.
// The same can be done to expand the size of the values blob.
//

class FlatEnvironment : public BaseEnvironment<FlatEnvironment> {
	public:

	FlatEnvironment(const std::string& path, const EnvOptions& opts);
	~FlatEnvironment();

	// void* allocateBytes(size_t n, size_t alignment=1);
	// void  freeBytes(void* firstByte, size_t n);
	// void reset();

	public:
		std::string path_;


	// -------------------------------------------------------------------
	//
	// The first 4096-byte block is the `fileMeta`.
	// It tells you how to address the mmaped file to get pointers to keys/vals,
	// per level.
	//
	// Not to be confused with the userMeta, which is a segment with json data.
	//
	struct LevelSpec {
		uint64_t keysCapacity=0, valsCapacity=0;
		uint64_t keysOffset=0, keysLength=0;
		uint64_t k2vsOffset=0;
		uint64_t valsOffset=0, valsLength=0;

		uint64_t nitemsUsed() { return keysLength   / sizeof(uint64_t); }
		uint64_t nitemsCap () { return keysCapacity / sizeof(uint64_t); }
	};
	struct FileMeta {
		uint64_t metaOffset=0, metaLength=0;
		LevelSpec levelSpecs[26];
	};

	static constexpr uint64_t fileMetaLength   = sizeof(uint64_t) * 2 + sizeof(LevelSpec) * 26;
	static constexpr uint64_t fileMetaCapacity = 4096;

	inline FileMeta* meta() {
		return reinterpret_cast<FileMeta*>(basePointer);
	}
	inline uint64_t* getKeys(int lvl) {
		return reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + meta()->levelSpecs[lvl].keysOffset);
	}
	inline uint64_t* getK2vs(int lvl) {
		return reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + meta()->levelSpecs[lvl].k2vsOffset);
	}
	inline void* getValues(int lvl) {
		return reinterpret_cast<void*>(static_cast<char*>(basePointer) + meta()->levelSpecs[lvl].valsOffset);
	}
	inline void* getValueFromIdx(int lvl, uint64_t idx) {
		const auto& spec = meta()->levelSpecs[lvl];
		uint64_t local_v_offset = reinterpret_cast<uint64_t*>(static_cast<char*>(basePointer) + spec.k2vsOffset)[idx];
		return static_cast<char*>(basePointer) + spec.valsOffset + local_v_offset;
	}

	uint64_t currentEnd = 0;
	uint64_t currentLvl = 0;

	bool writeKeys(uint64_t* keys, uint64_t len);
	bool writeValue(void* value, uint64_t len);

	bool beginLevel(int lvl);
	uint64_t growLevelKeys();
	uint64_t growLevelValues();

	void printFirstLastEightCurLvl();
	void printSomeInfo();

};


}
