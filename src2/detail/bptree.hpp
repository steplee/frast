#pragma once

#include <vector>
#include <string>
#include <stack>
#include <unordered_map>

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <fmt/color.h>

#include <cassert>

#include "env.h"




#define dprint(...) fmt::print(__VA_ARGS__);
// #define dprint(...) {};

#define strcat_(x, y) x ## y
#define strcat(x, y) strcat_(x, y)
#define PRINT_VALUE(x) \
    template <int> \
    struct strcat(strcat(value_of_, x), _is); \
    static_assert(strcat(strcat(value_of_, x), _is)<x>::x, "");






namespace frast {

	using TheEnvironment = PagedEnvironment;
	// using TheEnvironment = ArenaEnvironment;


	class BPTreeTester;


	//
	// A B+ Tree
	//
	//    o Typically the value should just be a vector of bytes.
	//
	//    o You want to vary M to make the size of a node near a multiple of \Environment::pageSize
	//      (For K = uint64_t, 30 works well)
	//
	//    o The root should be allocated once. It is never moved. That allows the user to always know
	//      where to load the tree from.
	//
	//    o Implementing this has been a less buggy pulling-out-my-hair process than I expected!
	//
	//    o TODO: Add ability to re-layout all used pages to another mmaped buffer and make sequential access better.
	//
	//    o TODO: Add ability to "freeze" all data and compress *ranges of leaf nodes* to chunked, contiguous pages.
	//            (i.e. boundary nodes allocate *all* leaves in *one* call to Environment::allocatePages())
	//
	//    o TODO: Actually when the data is "frozen", maybe just use sorted key array and byte offset to read values,
	//            no tree needed.
	//            In the case for frast, it seems possible to completely avoid a tree implementation:
	//                   Step 1) create environment to hold unaligned data.
	//                   Step 2) create dataset, allocating to next-free-pages in the first enviornment.
	//                   Step 3) all keys-values now known: copy from first environment to a new one, ordered.
	//              -> Because a tree is balancing during step 2, it is more space efficient,
	//                 without the tree you need a complicated in-place kv sorting algo, or just 2x the disk space.
	//              -> But it might be more time efficient to batch the sorting process in step 3.
	//				
	//		
	//    o WARNING: Every value added allocates it's own pages, there is no buffering or sharing.
	//               This is bad for storage.
	//
	//
	template <typename K, typename V, int M=30>
	class BPTree {

		friend class BPTreeTester;

		// we use a byte to indicate number of items in a node.
		static_assert(M < 256);

		public:

			static void prepareEmptyRoot(void* rootPtr);

			BPTree(void* rootPtr, TheEnvironment& env);

			bool insert(const K& k, const V& v);
			bool search(const K& k, V& v);

			uint32_t size() const { return treeSize; }

		// private:
		public:

			struct InnerNode {
				bool isRoot=false;
				bool isBoundary=false;
				uint8_t nresident=0;
				// void* sub[M];
				// K key[M];

				struct InteriorData {
					K key[M-1];
					InnerNode* children[M];
				};
				struct BoundaryData {
					K key[M];
					int leafSize[M];
					// LeafNode* leaves[M];
					void* leafPtr[M];
				};

				union {
					InteriorData id;
					BoundaryData bd;
				};
				// void* childPtr = 0; // Either Node* or V*
			};
			using RootNode = InnerNode;
			// static constexpr int s = sizeof(Node); PRINT_VALUE(s);

			/*

			// TODO: Revisit this: the idea is to combine ALL leaves in an inner node into one chunk of memory.

			struct LeafNodeHead {
				int offsets[M-1];
				int sizes[M-1];
				inline void* key(int i) { return static_cast<void*>(static_cast<char*>(sizes+M) + i); }
				inline void* val(int i) { return static_cast<void*>(static_cast<char*>(sizes+M) + i + sizeof(K)); }

				LeafNodeHead() = delete;
				LeafNodeHead(const LeafNodeHead& other) = delete;

				LeafNodeHead* allocate(TheEnvironment& e, int dataSize) {
					auto neededSize = sizeof(LeafNodeHead) + dataSize;
					size_t npages = (neededSize + e.pageSize() - 1) / e.pageSize();
					void* addr = e.allocatePages(npages);
					return static_cast<LeafNodeHead*>(addr);
				}
			};
			*/


			void split(std::stack<InnerNode*>& path, InnerNode* node);

			RootNode* root = nullptr;
			TheEnvironment& env;
			int levels = 0;
			uint32_t treeSize = 0;

			std::string dumpGraphviz();

		private:

				// static constexpr size_t nodeSize = sizeof(Node);
				// static constexpr size_t neededSize = nodeSize * M;
				// static constexpr size_t npages = (neededSize + TheEnvironment::pageSize() - 1) / TheEnvironment::pageSize();
				// PRINT_VALUE(neededSize);
				// PRINT_VALUE(npages);
			inline InnerNode* allocateInnerNode() {
				// size_t nodeSize = sizeof(InnerNode);
				// size_t neededSize = nodeSize * M;
				// size_t neededSize = nodeSize;
				// size_t npages = (neededSize + e.pageSize() - 1) / e.pageSize();
				// return static_cast<InnerNode*>(e.allocatePages(npages));
				return static_cast<InnerNode*>(env.allocateBytes(sizeof(InnerNode)));
			}


			/*
			inline std::pair<LeafNode*,int> searchLeafData(const K& k, V& v) {
				for (int i=0; i<M-1; i++) {
				}
			}
			*/

	};

	template <typename K, typename V, int M>
	void BPTree<K,V,M>::prepareEmptyRoot(void* rootPtr) {
		RootNode* root = new (rootPtr) RootNode{};
		// bzero(root->sub, sizeof(Node::sub));
		root->isRoot = true;
		root->isBoundary = true;
		root->nresident = 0;
	}

	template <typename K, typename V, int M>
	BPTree<K,V,M>::BPTree(void* rootPtr, TheEnvironment& e)
		: root(static_cast<RootNode*>(rootPtr)),
		  env(e) {
		levels = 1;
	}

	template <typename K, typename V, int M>
	bool BPTree<K,V,M>::search(const K& k, V& v) {
		return false;
	}

	// I think we want a recursive implementation (in addition to an explicit stack @path),
	// is because we may need to add back the @k argument at each recursive call, and its easier
	// to get compiler to do this with stack frames.
	template <typename K, typename V, int M>
	void BPTree<K,V,M>::split(std::stack<InnerNode*>& path, InnerNode* node) {
			// K* keyPtr = node->bd.key;
			// K node = node->bd.key[M/2+1];
			// K currentMedian = node->isBoundary ? node->bd.key[(node->nresident+1)/2] : node->id.key[(node->nresident+1)/2];
			// K currentMedian = node->isBoundary ? node->bd.key[(node->nresident)/2] : node->id.key[(node->nresident)/2];
			K currentMedian = node->isBoundary ? node->bd.key[(node->nresident)/2] : node->id.key[(node->nresident-1)/2];

			if (path.size() == 0) {
				// Root. Special Case.
				assert(node->isRoot);
				std::string myKeys;
				for (int i=0; i<(node->isBoundary ? M : (M-1)); i++)
					myKeys += fmt::format("{} ", node->isBoundary ? node->bd.key[i] : node->id.key[i]);
				dprint(fmt::fg(fmt::color::lime), " - split at root: keys: {}, median {}.\n", myKeys, currentMedian);

				// If we cannot add a new node, we must create a new level!
				if (node->nresident == M) {
					dprint(" - split at root :: need new level\n");

					InnerNode* child0 = allocateInnerNode();
					InnerNode* child1 = allocateInnerNode();

					child0->isBoundary = node->isBoundary;
					child1->isBoundary = node->isBoundary;
					child0->nresident = (node->nresident+1)/2;
					child1->nresident = (node->nresident  )/2;

					// This root node is no longer a boundary, but the imemdiate children are.
					if (node->isBoundary) {

						int child0i=0, child1i=0;
						int ii = 0;
						for (int j=0; j<child0->nresident; ii++, j++) {
							child0->bd.key[j] = node->bd.key[ii];
							child0->bd.leafSize[j] = node->bd.leafSize[ii];
							child0->bd.leafPtr[j] = node->bd.leafPtr[ii];
							dprint(" - copy {} {} {} to child0\n",
								child0->bd.key[j], child0->bd.leafSize[j], child0->bd.leafPtr[j]);
						}
						for (int j=0; j<child1->nresident; ii++, j++) {
							child1->bd.key[j] = node->bd.key[ii];
							child1->bd.leafSize[j] = node->bd.leafSize[ii];
							child1->bd.leafPtr[j] = node->bd.leafPtr[ii];
							dprint(" - copy {} {} {} to child1\n",
								child1->bd.key[j], child1->bd.leafSize[j], child1->bd.leafPtr[j]);
						}

						node->isBoundary = false;
						node->nresident = 2;
						node->id.key[0] = currentMedian;
						node->id.children[0] = child0;
						node->id.children[1] = child1;

						dprint(" - split at root :: converted root boundary -> interior, with 2 children (nr {} {}) (k {})\n",
								child0->nresident, child1->nresident, node->id.key[0]);

					} else {

						int child0i=0, child1i=0;
						int ii = 0;
						for (int j=0; j<child0->nresident; ii++, j++) {
							child0->id.key[j] = node->id.key[ii];
							child0->id.children[j] = node->id.children[ii];
							dprint(" - copy {} {} to child0\n",
								child0->id.key[j], (void*)child0->id.children[j]);
						}
						for (int j=0; j<child1->nresident; ii++, j++) {
							if (ii != node->nresident)
								child1->id.key[j] = node->id.key[ii];
							child1->id.children[j] = node->id.children[ii];
							dprint(" - copy {} {} to child1\n",
								child1->id.key[j], (void*)child1->id.children[j]);
						}

						node->nresident = 2;
						node->id.key[0] = currentMedian;
						node->id.children[0] = child0;
						node->id.children[1] = child1;
						dprint(" - split at root :: interior, with 2 children (nr {} {}) (k {})\n",
								child0->nresident, child1->nresident, node->id.key[0]);


					}
				} else {
					dprint(" - split at root :: DO NOT need new level\n");
					assert(false);
				}

			} else {
				// We know that parent is NOT a boundary node.
				InnerNode* parent = path.top();
				assert(not parent->isBoundary);

				if (parent->nresident == M) {
					// recursive case.

					// Recurse. After this we'll have space to insert
					dprint(fmt::fg(fmt::color::magenta), " - split must recurse!\n");
					path.pop();
					split(path, parent);

					// WARNING: Parent may have changed.
					InnerNode* findIt = root;
					while (path.size()) path.pop();
					path.push(findIt);
					const auto &k = node->id.key[0];
					dprint(" - returned from recursive split, finding new parent for k {}\n", k);
					while (findIt != node) {
						fmt::print(" - drill\n");
						bool didSet = false;
						for (int i=0; i<findIt->nresident-1; i++) {
							fmt::print(" - look at {}\n", findIt->id.key[i]);
							if (k < findIt->id.key[i]) {
								findIt = findIt->id.children[i];
								path.push(findIt);
								didSet = true;
								break;
							}
						}
						if (not didSet) {
							findIt = findIt->id.children[findIt->nresident-1];
							path.push(findIt);
						}
					}
					path.pop();
					parent = path.top();
					dprint(" - new parent {}\n", (void*)(parent));

				}

				if (1) {

					assert(parent->nresident < M);

					/*
				} else {
					*/


					// Insert the current median at correct location in parent.
					// Split the @node in half, with the second half filling a new inner node.

					// @idx is the new split index of the <old,new> pair of children.
					int idx = 0;
					while (currentMedian >= parent->id.key[idx] and idx < parent->nresident-1)
						idx++;

					dprint(fmt::fg(fmt::color::green), " - split insert at parent idx {} (/ {})\n", idx,parent->nresident);

					// WARNING: Indexing here is def wrong.

					for (int i=parent->nresident-1; i>idx; i--)
						parent->id.key[i] = std::move(parent->id.key[i-1]);
					for (int i=parent->nresident; i>idx; i--)
						parent->id.children[i] = std::move(parent->id.children[i-1]);

					// Set the median as a new key in this inner node.
					parent->nresident++;
					parent->id.key[idx] = currentMedian;

					std::string newParentKeys;
					for (int i=0; i<parent->nresident-1; i++) newParentKeys += fmt::format("{} ", parent->id.key[i]);
					dprint(fmt::fg(fmt::color::green), " - new parent keys: {}\n", newParentKeys);

					// Allocate new 'sibiling'. It will have same type as node.
					parent->id.children[idx+1] = allocateInnerNode();
					InnerNode* sibiling = parent->id.children[idx+1];
					sibiling->isBoundary = node->isBoundary;

					// Fill it with half the data.
					int off = (node->nresident+1)/2;
					int nsib = node->nresident - off;
					if (node->isBoundary) {
						for (int i=0; i<nsib; i++) {
							sibiling->bd.key[i] = node->bd.key[i+off];
							sibiling->bd.leafPtr[i] = node->bd.leafPtr[i+off];
							sibiling->bd.leafSize[i] = node->bd.leafSize[i+off];
						}
						sibiling->nresident = nsib;
						node->nresident = off;
					} else {
						for (int i=0; i<nsib; i++) {
							sibiling->id.key[i] = node->id.key[i+off];
							sibiling->id.children[i] = node->id.children[i+off];
						}
						sibiling->nresident = nsib;
						node->nresident = off;
					}

				}
			}
	}

	template <typename K, typename V, int M>
	bool BPTree<K,V,M>::insert(const K& k, const V& v) {

		InnerNode* curr = root;
		std::stack<InnerNode*> path;

		dprint(" - [insert] k={} v={}\n", k,v);

		auto allocateAndCopyLeaf = [&](const V& v) {
			// size_t npages = (v.size() + env.pageSize() - 1) / env.pageSize();
			// void* buf = env.allocatePages(npages);
			void* buf = env.allocateBytes(v.size());
			memcpy(buf, v.data(), v.size());
			return buf;
		};
		auto deleteLeaf = [&](size_t size, void* ptr) {
			// size_t npages = (v.size() + env.pageSize() - 1) / env.pageSize();
			// env.freePages(ptr, npages);
			env.freeBytes(ptr, v.size());
		};

		while (true)
		{

			if (curr->isBoundary) {

				// Find where to insert
				int idx = 0;
				while (k > curr->bd.key[idx] and idx < curr->nresident) idx++;
				dprint(" - [insert] (boundary) determined to insert at {}'th entry (of {}).\n", idx, curr->nresident);

				if (curr->bd.key[idx] == k) {

					// Exact match, therefore no movement is needed. Just replace leaf
					dprint(" - [insert] exact match. easy case.\n");
					deleteLeaf(curr->bd.leafSize[idx], curr->bd.leafPtr[idx]);
					curr->bd.key[idx] = k;
					curr->bd.leafPtr[idx] = allocateAndCopyLeaf(v);
					curr->bd.leafSize[idx] = v.size();
					return false;

				} else {
					// If too many elements, we must split.
					if (curr->nresident == M) {

						// If we are splitting the current node, we know keyPtr should
						// have the full M elements.

						dprint(fmt::fg(fmt::color::yellow), " - insert must split.\n");
						split(path, curr);

						// It is possible that this @curr is no longer a boundary node.
						// In that case, it is easiest to recurse
						// Actually: always do because we could have changed the order and
						// now be in wrong subtree..
						return insert(k,v);


						/*
						// Re-find idx (after modification from split)
						int idx = 0;
						while (k >= curr->bd.key[idx] and idx < curr->nresident) idx++;
						dprint(" - insert returned from split (new size {}), new idx {}.\n", curr->nresident, idx);
						assert(curr->nresident < M);

						// Shift all data to the right of @idx towards right once.
						for (int i=curr->nresident; i>idx; i--) {
							curr->bd.key[i] = std::move(curr->bd.key[i-1]);
							curr->bd.leafPtr[i] = std::move(curr->bd.leafPtr[i-1]);
						}
						curr->bd.key[idx] = k;
						curr->bd.leafPtr[idx] = allocateAndCopyLeaf(v);
						curr->bd.leafSize[idx] = v.size();
						curr->nresident++;
						*/


					} else {

						/*
						// Otherwise, if this is at the end, we do not need to move other data.
						if (idx == curr->nresident) {
							curr->bd.k[idx] = k;
							curr->bd.v[idx] = allocateAndCopyLeaf(v);
							curr->nresident++;
						}

						// Otherwise, we must move old data and create new node.
						else {
							// Shift all data to the right of @idx towards right once.
							for (int i=curr->nresident; i>idx; i--) {
								curr->k[i] = std::move(curr->k[i-1]);
								curr->leafPtr[i] = std::move(curr->leafPtr[i-1]);
							}
							curr->bd.k[idx] = k;
							curr->bd.v[idx] = allocateAndCopyLeaf(v);
							curr->nresident++;
						}
						*/

						// Shift all data to the right of @idx towards right once.
						dprint(" - insert shifting [{} - {}], writing at {}.\n", idx, curr->nresident, idx);
						for (int i=curr->nresident; i>idx; i--) {
							curr->bd.key[i] = std::move(curr->bd.key[i-1]);
							curr->bd.leafPtr[i] = std::move(curr->bd.leafPtr[i-1]);
						}
						curr->bd.key[idx] = k;
						curr->bd.leafPtr[idx] = allocateAndCopyLeaf(v);
						curr->bd.leafSize[idx] = v.size();
						curr->nresident++;

						treeSize++;
						return false;
					}
				}

			} else {

				// Find which child InnerNode to visit next.
				int childIdx = 0;
				while (k >= curr->id.key[childIdx] and childIdx < curr->nresident-1)
					childIdx++;

				dprint(" - [insert] (interior) will drill down at {}'th entry (/ {}).\n", childIdx, curr->nresident);

				// Set it and loop
				path.push(curr);
				curr = curr->id.children[childIdx];
			}
		}

		assert(false);
		return false;
	}






	template <typename K, typename V, int M>
	std::string BPTree<K,V,M>::dumpGraphviz() {
		std::stack<std::pair<InnerNode*,int>> st;
		st.push({root,0});

		std::string O;

		O += fmt::format("digraph {{\n");
		O += fmt::format("graph [pad=\"0\", nodesep=\".01\", ranksep=\"1\"];\n");
		O += fmt::format("node [shape=plaintext];\n"); // Make value nodes have no border

		int innerCnt = 0;
		int  leafCnt = 0;

		std::unordered_map<void*, std::string> nodeNames;
		std::vector<std::string> nodeRanks[10];

		while (not st.empty()) {
			auto it = st.top();
			auto curr = it.first;
			auto depth = it.second;
			st.pop();

			std::string label = "<\n <TABLE>";

			std::string nodeName = fmt::format("in{}", innerCnt);
			nodeNames[curr] = nodeName;
			nodeRanks[depth].push_back(nodeName);
			innerCnt++;

			// Keys
			label += "<TR>";
			// for (int i=0; i<(curr->isBoundary ? M-1 : M); i++) {
			for (int i=0; i<(curr->isBoundary ? M : (M-1)); i++) {
				if (i < (curr->isBoundary ? curr->nresident : (curr->nresident-1)))
					label += fmt::format("<TD PORT=\"k{}\">{}</TD>", i, curr->isBoundary ? curr->bd.key[i] : curr->id.key[i]);
				else
					label += fmt::format("<TD PORT=\"k{}\"></TD>", i);
			}
			label += "</TR>";

			// Children
			label += "<TR>";
			for (int i=0; i<M; i++) {
				if (i < curr->nresident)
					label += fmt::format("<TD PORT=\"c{}\">*</TD>", i);
				else
					label += fmt::format("<TD PORT=\"c{}\">.</TD>", i);
			}
			label += "</TR>";
			label += "</TABLE> >";

			O += fmt::format("{} [shape=plaintext label={}];\n", nodeName, label);

			// add the leaf nodes here.
			if (curr->isBoundary) {
				// dprint(" - viz: boundary with {} children\n", curr->nresident);
				for (int i=0; i<curr->nresident; i++) {
					auto leaf = curr->bd.leafPtr[i];
					// std::string nodeName = fmt::format("leaf{}", leafCnt++);
					std::string nodeName = fmt::format("leaf{}", curr->bd.key[i]);
					nodeNames[leaf] = nodeName;
					nodeRanks[depth+1].push_back(nodeName);
					// O += fmt::format("{} [shape=record label={}];\n", nodeName, leafCnt-1);
					O += fmt::format("{} [label={}];\n", nodeName, *static_cast<V*>(curr->bd.leafPtr[i]));
				}
			} else {
				// push
				// dprint(" - viz: interior with {} children\n", curr->nresident);
				for (int i=0; i<curr->nresident; i++)
					st.push({curr->id.children[i],depth+1});
			}
		}

		st.push({root,0});
		while (not st.empty()) {
			auto it = st.top();
			auto curr = it.first;
			auto depth = it.second;
			st.pop();
			for (int i=0; i<curr->nresident; i++) {
				O += fmt::format("{}:c{} -> {}\n", nodeNames[curr], i, curr->isBoundary ? nodeNames[curr->bd.leafPtr[i]] : nodeNames[curr->id.children[i]]);
			}

			if (not curr->isBoundary)
				for (int i=0; i<curr->nresident; i++)
					st.push({curr->id.children[i],depth+1});
		}

		for (int d=0; d<10; d++) {
			if (nodeRanks[d].size()) {
				O += "{rank=same; ";
				for (auto name : nodeRanks[d]) O += name + "; ";
				O += "}\n";
			}
		}

		O += fmt::format("}}\n");
		return O;
	}


}

#undef dprint
