#include "detail/env.h"
#include "coordinates.h"

#include <cassert>


namespace frast {

	constexpr uint64_t INVALID_OFFSET = 0xfffffffffffffffflu;

	struct KeyAndValRef {
		BlockCoordinate k;
		uint64_t byteOffset; // Where the key is, in the mmap.
	};

	struct KeyBlock {
		KeyAndValRef[32768];
		uint64_t n=0;
		uint64_t nextByteOffset = 0; // Next node

	};

	class FlatWriter {

		public:

			FlatWriter(const std::string& path, const EnvOptions& opts);

			void reset();

			KeyBlock* appendList(KeyBlock* curr);
			KeyBlock* allocateKeyBlock();

		private:

			ArenaEnvironment env;

			KeyAndValRef* headPerLevel[MAX_LVLS];

	};

	FlatWriter::FlatWriter(const std::string& path, const EnvOptions& opts)
		: env(path, opts)
	{
	}

	void FlatWriter::reset() {
		env.reset();
	}


	KeyBlock* FlatWriter::allocateKeyBlock() {
		auto out = reinterpret_cast<KeyBlock*>(env.allocateBytes(sizeof(KeyBlock)));
		return out;
	}

	KeyBlock* FlatWriter::appendList(KeyBlock* curr) {
		auto out = allocateKeyBlock();
		assert(curr->nextByteOffset == 0);
		curr->nextByteOffset = out - dataPointer;
		return out;
	}

}
