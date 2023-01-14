#include "flat_writer.h"

#include <algorithm>

namespace frast {

	FlatWriter::~FlatWriter() {

		for (int i=0; i<MAX_LVLS; i++) {
			if (headPerLevel[i] != 0) {
				fmt::print(" - [~FlatWriter] saving lvl {} with off {} :: {} {}\n", i, (char*)headPerLevel[i] - ((char*)env.dataPointer), (void*)headPerLevel[i], (void*)env.dataPointer);
				header->headOffsets[i] = (char*)headPerLevel[i] - ((char*)env.dataPointer);
			}
		}

	}

	FlatWriter::FlatWriter(const std::string& path, const EnvOptions& opts)
		: env(path, opts)
	{
		// TODO: Parse header, set base pointers
		parseOrCreateHeaderThenSetup();
	}

	void FlatWriter::parseOrCreateHeaderThenSetup() {
		// The header is a 16380 json blob followed by MAX_LVLS uint64_t's that are byte offsets
		// to the heads per level.
#if 0
		if (env.fileIsNew()) {
			fmt::print(" - [FlatWriter::parseOrCreateHeaderThenSetup] new file, creating header.\n");
			// JSON.
			env.allocateBytes(16384);

			// Pointers.
			for (int i=0; i<MAX_LVLS; i++) {
				/*
				KeyBlock* ptr = allocateKeyBlock();
				headPerLevel[i] = currPerLevel[i] = ptr;
				fmt::print(" - ptr {} -> {}, {}\n", i, (void*)ptr, ((char*)ptr - (char*)env.dataPointer));
				*/
				headPerLevel[i] = currPerLevel[i] = nullptr;

				void* ptr = env.allocateBytes(sizeof(KeyBlock*), 8);
				// void* ptr = env.allocateBytes(sizeof(KeyBlock*), 0);
				memset(ptr, 0, 8);
			}

		} else {
			fmt::print(" - [FlatWriter::parseOrCreateHeaderThenSetup] old file, parsing header.\n");
			char* json = (char*) (env.dataPointer)+0;

			/*
			KeyBlock* pointers[26] = reinterpret_cast<KeyBlock**>(static_cast<char*>(env.dataPointer)+16384);
			for (int i=0; i<MAX_LVLS; i++) {
				headPerLevel[i] = currPerLevel[i] = pointers[i];
				fmt::print(" - ptr {} -> {}\n", i, (void*)pointers[i]);
			}
			*/

			uint64_t off = 16384;
			for (int i=0; i<MAX_LVLS; i++) {

				size_t storedOffset = *reinterpret_cast<uint64_t*>((char*)env.dataPointer + off);

				KeyBlock* ptr;
				if (storedOffset == 0) {
					ptr = nullptr;
				} else {
					ptr = (KeyBlock*) ((char*)env.dataPointer + storedOffset);
				}

				headPerLevel[i] = currPerLevel[i] = ptr;

				if (ptr) {
				int j=0;
				while (currPerLevel[i]->nextByteOffset != 0) {
					currPerLevel[i] = reinterpret_cast<KeyBlock*>((char*)env.dataPointer + (currPerLevel[i]->nextByteOffset));
					j++;
				}
				fmt::print(" - Level {} block {}, offset {}\n", i, j, currPerLevel[i]->n);
				}

				off += sizeof(KeyBlock*); // 8
			}
		}
#endif

		if (env.fileIsNew()) {
			fmt::print(" - [FlatWriter::parseOrCreateHeaderThenSetup] new file, creating header, size {}.\n", sizeof(Header));
			header = (Header*) env.allocateBytes(sizeof(Header));
		} else {
			header = ((Header*)env.dataPointer) + 0;

			for (int i=0; i<MAX_LVLS; i++) {
				KeyBlock* head = nullptr;
				KeyBlock* curr = nullptr;

				uint64_t offset = header->headOffsets[i];
				int blkId = 0;
				if (offset != 0) {
					curr = head = (KeyBlock*) ((char*)env.dataPointer+offset);
					while (offset != 0) {
						curr = (KeyBlock*) ((char*)env.dataPointer+offset);
						offset = curr->nextByteOffset;
						blkId++;
					}
					fmt::print(" - [parseOrCreateHeaderThenSetup] loading lvl {}, blk {}, off {}\n", i, blkId, curr->n);
				}

				headPerLevel[i] = head;
				currPerLevel[i] = curr;
			}

		}
	}

	void FlatWriter::reset() {
		env.reset();
	}


	KeyBlock* FlatWriter::allocateKeyBlock() {
		auto out = reinterpret_cast<KeyBlock*>(env.allocateBytes(sizeof(KeyBlock), 8));
		return out;
	}

	KeyBlock* FlatWriter::appendList(KeyBlock* curr) {
		auto out = allocateKeyBlock();
		assert(curr->nextByteOffset == 0);
		curr->nextByteOffset = (char*)out - (char*)env.dataPointer;

		return out;
	}


	KeyAndValRef& FlatWriter::getNextKeyRef(int lvl) {
		std::lock_guard<std::mutex> lck(envMtx);

		KeyBlock* kvr = currPerLevel[lvl];

		if (kvr == nullptr) {
			fmt::print(" - [getNextKeyRef] Creating new level {}\n", lvl);
			kvr = headPerLevel[lvl] = currPerLevel[lvl] = allocateKeyBlock();
		}

		if (kvr->n == KeyBlock::N-1) {
			fmt::print(" - [getNextKeyRef] need new block for lvl {}\n", lvl);
			// We need a new block.
			currPerLevel[lvl] = appendList(currPerLevel[lvl]);
			kvr = currPerLevel[lvl];
		}

		return kvr->entries[kvr->n++];
	}

	uint64_t FlatWriter::getValueBufferOffset(size_t size, size_t align) {
		std::lock_guard<std::mutex> lck(envMtx);

		void* ptr = env.allocateBytes(size, align);
		return ((char*)ptr) - (char*)env.dataPointer;
	}








	void FlatWriterPhase2::buildEntries(const std::string& inPath, const EnvOptions& opts) {
		FlatWriter fw(inPath, opts);

		for (int lvl=0; lvl<MAX_LVLS; lvl++) {

			fmt::print(" - Scanning lvl {}\n", lvl);
			KeyBlock* curr = fw.headPerLevel[lvl];
			while (true) {
				for (int i=0; i<curr->n; i++) {
					entriesPerLevel[lvl].push_back(curr->entries[i]);
				}
			}

			if (entriesPerLevel[lvl].size()) {
				fmt::print(" - Sorting {} entries\n", entriesPerLevel[lvl].size());
				std::sort(entriesPerLevel[lvl].begin(), entriesPerLevel[lvl].end(),
						[](const KeyAndValRef& a, const KeyAndValRef& b) { return a.k.c < b.k.c; });
			}
		}

	}


	void FlatWriterPhase2::runCopyJob() {

		size_t totalSize = 0;

		for (int lvl=0; lvl<MAX_LVLS; lvl++) {
			size_t keySize=0, valSize=0, padSize=0;
			keySize = sizeof(BlockCoordinate) * entriesPerLevel[lvl].size();
			for (const auto& entry : entriesPerLevel[lvl])
				valSize += entry.byteSize;

			while ((keySize + valSize + padSize) % 4096 != 0)
				padSize++;
		}

		fmt::print(" - Need {} bytes ({} Gb)\n", totalSize, totalSize/(1lu<<30lu));

	}



}
