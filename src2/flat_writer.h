#include "detail/env.h"
#include "coordinates.h"

#include <mutex>
#include <cassert>
#include <vector>


namespace frast {

	constexpr uint64_t INVALID_OFFSET = 0xfffffffffffffffflu;

	struct KeyAndValRef {
		BlockCoordinate k;
		uint64_t byteSize=0;
		uint64_t byteOffset=0; // Where the key is, in the mmap.
	};

	struct KeyBlock {
		constexpr static int N = 32768;
		KeyAndValRef entries[N];
		uint64_t n=0;
		uint64_t nextByteOffset = 0; // Next node

	};

	class FlatWriterPhase2;

	class FlatWriter {

		friend class FlatWriterPhase2;

		public:

			FlatWriter(const std::string& path, const EnvOptions& opts);
			~FlatWriter();

			void reset();

			// Each Worker will do this for every tile it wishes to write:
			//		1) Call fw->getNextKeyRef(lvl)
			//		2) Do background processing
			//		3) Call fw->getValueBuffer(valSize)
			//		4) Call fw->commitKeyValue(kv) <-------- Do not need.

			KeyAndValRef& getNextKeyRef(int lvl);
			uint64_t getValueBufferOffset(size_t size, size_t align=1);
			// bool commitKeyValue(KeyAndValRef& kvr, void* value);
			

			inline void* offToPtr(uint64_t o) const {
				return ((char*)env.dataPointer) + o;
			}

		private:

			struct Header {
				char json[16384] = {0};
				uint64_t headOffsets[MAX_LVLS] = {0};
			};

			ArenaEnvironment env;
			std::mutex envMtx;

			Header* header;

			KeyBlock* headPerLevel[MAX_LVLS] = {0};
			KeyBlock* currPerLevel[MAX_LVLS] = {0};

			KeyBlock* appendList(KeyBlock* curr);
			KeyBlock* allocateKeyBlock();

			void parseOrCreateHeaderThenSetup();

	};


	// Has file format like:
	//
	//       - Fixed size header.
	//       - Per-Level keys + byte offsets.
	//       - Binary Data that the byte offsets point to.
	//
	//


	class FlatWriterPhase2 {

		public:

			FlatWriterPhase2(const std::string& outPath, const std::string& inPath, const EnvOptions& opts);

		private:

			ArenaEnvironment env;

			std::vector<KeyAndValRef> entriesPerLevel[MAX_LVLS];

			void buildEntries(const std::string& inPath, const EnvOptions& opts);

			void runCopyJob();



	};


}
