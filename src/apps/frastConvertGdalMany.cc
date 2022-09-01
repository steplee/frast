
#include "frastConvertGdal.hpp"

#include <Eigen/Geometry>
#include <unsupported/Eigen/BVH> // Exaclty what I needed, nice!

#include <fstream>

//
// Like `frastConvertGdal`, but for the case when it is impractical to have one input file (even one VRT)
// This is the case when you have many different projections and/or color formats.
// Expect this binary to be slower than the other -- it does a bit more,
// including blending every pixel possibly multiple times.
//
// Here is the full process
//
// 1) Build Kd-Tree from all listed files
// 2) Determine tlbr corners (unless user provides their own)
// 3) Create output file
// 4) For each output tile:
//        Query tree, for each dataset it intersects:
//				Inv-warp the tlbr of the tile into the projected coordinates
//				Determine AABB
//				Sample using gdal's rasterIO
//				Warp the projected rectangle to a web mercator quad
//				Color convert, if needed
//				Add 1.0 to alpha channel, if not nodata, for each pixel
//		  Blend final result by dividing by each pixel by (alpha channel + epsilon)
//		  Convert to uint8_t
//		  Fin.
//
// The initial implementation will loop over each tile row-by-row, and won't be multi-threaded.
// Can worry about disk cache maximization and about multi-threading later.
//


namespace {

	using namespace Eigen;

	using Box2f = AlignedBox<float, 2>;

	struct Node {
		GdalDset* dset;
		Box2f bbox;
	};
	inline Box2f bounding_box(const Node& node) { return node.bbox; }

     // bool intersectVolume(const BVH::Volume &volume) //returns true if volume intersects the query
     // bool intersectObject(const BVH::Object &object) //returns true if the search should terminate immediately
	struct DsetIntersector {
		Box2f query;
		std::vector<GdalDset*> hitDatasets;

		inline DsetIntersector(const Box2f& query_) : query(query_) {
			hitDatasets.reserve(8);
		}

		using BVH = KdBVH<float, 2, Node>;

		bool intersectVolume(const BVH::Volume &volume) {
			return volume.intersects(query);
		}
		bool intersectObject(const BVH::Object &object) {
			bool intersects = query.intersects(object.bbox);
			if (intersects) {
				hitDatasets.push_back(object.dset);
			}
			return false;
		}
	};

	struct InvertedDatasetList {

		std::vector<Node> nodes;
		KdBVH<float, 2, Node> tree;

		inline InvertedDatasetList(std::vector<Node>&& nodes_) :
			nodes(std::move(nodes_)),
			tree(nodes.begin(), nodes.end()) {
			}

		// No copies!
		InvertedDatasetList(const InvertedDatasetList&) = delete;
		InvertedDatasetList& operator=(const InvertedDatasetList&) = delete;
		InvertedDatasetList(InvertedDatasetList&&) = default;
		InvertedDatasetList& operator=(InvertedDatasetList&&) = default;

		inline std::vector<GdalDset*> search(const Box2f& query) {
			DsetIntersector xsect(query);
			BVIntersect(tree, xsect);
			return std::move(xsect.hitDatasets);
		}
		
	};

	// Parse the index file, which is just a filename per line
	inline std::vector<std::string> getFilesFromIndexFile(const std::string &infile) {
		std::ifstream ifs(infile);
		std::string buf;
		std::vector<std::string> out;
		while (std::getline(ifs, buf)) {
			out.push_back(buf);
		}
		return out;
	}

	inline InvertedDatasetList loadFiles(const std::string &infile, const Image::Format& outFormat) {
		auto files = getFilesFromIndexFile(infile);

		std::vector<Node> nodes;
		for (const auto& file : files) {
			Node node;
			node.dset = new GdalDset(file, outFormat);
			auto& tlbr_uwm = node.dset->tlbr_uwm;
			node.bbox = Box2f { tlbr_uwm.head<2>().cast<float>(), tlbr_uwm.tail<2>().cast<float>() };
			nodes.push_back(std::move(node));
		}

		return InvertedDatasetList { std::move(nodes) };
	}
}



int main(int argc, char** argv) {

	std::string inIndex;
	auto idl = loadFiles(inIndex, Image::Format::GRAY);

	return 0;
}
