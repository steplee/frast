
#include "frastConvertGdal.hpp"

#include <Eigen/Geometry>
#include <unsupported/Eigen/BVH> // Exaclty what I needed, nice!

#include <fstream>
#include <fmt/core.h>
#include <fmt/ostream.h>


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
// Artifacts visible when blending:
//		GDAL's rasterIO fails if you read a partial out of bounds block. But we still need the valid pixels.
//		To allow this, my GdalDset class samples the valid box, then warps to the asked for size,
//		so that it looks normal to the caller. Only this may not be pixel perfect, and the edges may fade to black.
//		So when blending we can get artifacts.
//
//		Some solutions are to slightly rescale the warped image so that it makes sure to cover, to
//		include a floating point mask with the warp, etc. But that's complicated.
//
//		Instead, I'll go with push-pull filtering, which blurs both the color and alpha
//		at the same time. Then in a final pass, divides by the accumulated alpha.
//		I've used a similar technique to fill in holes in a depthmap.
//


namespace {

	using namespace Eigen;

	using Vector2l = Eigen::Matrix<int64_t,2,1>;
	using Array2l = Eigen::Array<int64_t,2,1>;
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

		inline InvertedDatasetList() {}
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

		/*
		return {
			"/data/naip/vegas2020/m_3511507_nw_11_060_20190706.tif",
			"/data/naip/vegas2020/m_3511515_nw_11_060_20190706.tif",
			"/data/naip/vegas2020/m_3611457_sw_11_060_20190706.tif",
		};
		*/

		std::ifstream ifs(infile);
		std::string buf;
		std::vector<std::string> out;
		while (std::getline(ifs, buf)) {
			out.push_back(buf);
		}
		return out;
	}

	inline InvertedDatasetList loadFilesBuildIndex(const std::string &infile, const Image::Format& outFormat) {
		auto files = getFilesFromIndexFile(infile);

		std::vector<Node> nodes;
		for (const auto& file : files) {
			Node node;
			node.dset = new GdalDset(file, outFormat);
			node.dset->clampToBorder = false;
			auto& tlbr_uwm = node.dset->tlbr_uwm;
			node.bbox = Box2f { tlbr_uwm.head<2>().cast<float>(), tlbr_uwm.tail<2>().cast<float>() };
			nodes.push_back(std::move(node));
		}

		return InvertedDatasetList { std::move(nodes) };
	}


	struct ConvertParams {
		InvertedDatasetList idl;

		Box2f outputBox;
		int level;

		std::string outPath = "test.ft";
	};

	// not worrying about precision...
	Vector2f uwm_from_int(const Vector2l& i, int z) {
		assert(z > 0);
		float mul = 1.f / (1l << (z-1));
		return Vector2f {
			static_cast<float>(i(0)) * mul - 1.f,
			static_cast<float>(i(1)) * mul - 1.f
		};
	}

	Vector2l int_from_uwm(const Vector2f& u, int z) {
		assert(z > 0);
		float mul = (float) (1l << (z-1));
		return Vector2l {
			static_cast<int64_t>((u(0) + 1.f) * mul),
			static_cast<int64_t>((u(1) + 1.f) * mul)
		};
	}

	int run_it(ConvertParams& cp) {

		int level = cp.level;
		auto& idl = cp.idl;
		for (int i=0; i<idl.nodes.size(); i++)
			fmt::print(" - dset {} bounds {}\n", i, idl.nodes[i].dset->tlbr_uwm.transpose());

		Box2f query0 { Vector2f{-1,-1}, Vector2f{1,1} };
		auto near = cp.idl.search(query0);
		fmt::print(" - n near {}\n", near.size());

		Box2f query1 { idl.nodes[0].dset->tlbr_uwm.template head<2>().cast<float>(), idl.nodes[0].dset->tlbr_uwm.template tail<2>().cast<float>() };
		near = cp.idl.search(query1);
		fmt::print(" - n near {}\n", near.size());


		// Vector2l tl = (cp.outputBox.min() * WebMercatorMapScale).cast<int64_t>();
		// Vector2l br = (cp.outputBox.max() * WebMercatorMapScale).cast<int64_t>();
		Vector2l tl = int_from_uwm(cp.outputBox.min(), level);
		Vector2l br = int_from_uwm(cp.outputBox.max(), level);
		Array2l sz = br - tl;
		assert( (sz>0).all() );

		fmt::print(" - From outputBox {} => {}\n", cp.outputBox.min().transpose(), cp.outputBox.max().transpose());
		fmt::print(" - From outputBox {} => {}\n", WebMercatorMapScale*cp.outputBox.min().transpose(), WebMercatorMapScale*cp.outputBox.max().transpose());
		fmt::print(" - Have range     {} => {}\n", tl.transpose(), br.transpose());
		fmt::print(" - sz hw          {}\n", sz.transpose());

		auto outFormat = Image::Format::RGB;
		Image tileImage { 256, 256, outFormat };
		tileImage.alloc();

		std::vector<float> tmpBuffer4 (256*256*4, 0.f);
		// std::vector<uint8_t> tmpBuffer3 (256*256*3, 0);
		Image blendedImage { 256, 256, outFormat };
		blendedImage.alloc();

		DatabaseOptions opts;
		DatasetWritable outDset { cp.outPath , opts };
		outDset.setFormat((uint32_t)outFormat);
		outDset.setTileSize(256);
		// outDset.configure(CONVERT_THREADS, WRITER_NBUF);
		outDset.configure(1,32);
		outDset.sendCommand(DbCommand{DbCommand::BeginLvl,level});

		/*
		for (int yy=0; yy<sz(1); yy++) {
			for (int xx=0; xx<sz(0); xx++) {
				int y = yy + tl(1), x = xx + tl(0);
				*/
		//
		// Try to improve locality of reference vs the naive order
		// Best and still easy order would depend on the average input file size
		//
		for (int yy=0; yy<1+sz(1)/8; yy++)
			for (int xx=0; xx<1+sz(0)/8; xx++)
				for (int dy=0; dy<8; dy++) {
					for (int dx=0; dx<8; dx++) {

				int y = yy*8 + dy + tl(1), x = xx*8 + dx + tl(0);

				if (y >= br(1) or x >= br(0)) continue;

				Box2f currentBox {
					uwm_from_int(Vector2l{x,y}, level),
					uwm_from_int(Vector2l{x+1,y+1}, level) };

				// fmt::print(" - tile {} {} {} search {}\n", level, y, x, currentBox.min().transpose());

				auto relevantDsets = idl.search(currentBox);
				if (relevantDsets.size() == 0) continue;
				fmt::print(" - tile {} {} {} needs {} datasets\n", level, y, x, relevantDsets.size());

				// NOTE: Assume RGB for now, but should also work with GRAY...
				using ImgArr4 = Array<float, 256*256, 4, RowMajor>;
				using ImgArr3u = Array<uint8_t, 256*256, 3, RowMajor>;
				// ImgArr4 acc;
				Map<ImgArr4> acc { tmpBuffer4.data() };
				Map<ImgArr3u> fin { blendedImage.buffer };
				acc.setZero();

				// Accumulate
				// TODO: Only divide if there is >1 matching dataset
				for (auto& dset : relevantDsets) {
					if (!dset->getTile(tileImage, level, y, x)) {
						Map<ImgArr3u> mapImg { tileImage.buffer };
						acc.leftCols(3) += mapImg.cast<float>();

						// WARNING: Has bad seams
						// acc.rightCols(1) += (mapImg > 5).rowwise().any().cast<float>();
						// WARNING: Not even this works well
						// WARNING: It must be coming from the alignment of the tiffs and/or the warpPerspective call for partial reads
						// TODO: Try to either fix that, OR implement push-pull filtering to blend-away pixels close to borders.
						auto mu = mapImg.cast<float>().rowwise().mean();
						acc.rightCols(1) += (mu * mu).cwiseMin(30.f) / 30.f;
						// acc.rightCols(1) += 1.f;
					}
					// else assert(false);
				}

				// Blend
				// fin = (acc.leftCols(3) / (.001f+acc.rightCols(1)).transpose()).cwiseMin(255.1f).cwiseMax(0.f).cast<uint8_t>();
				for (int i=0; i<256*256; i++)
					fin.row(i) = (acc.block<1,3>(i,0) / (.0001f + acc(i,3))).cwiseMin(255.f).cwiseMax(0.f).cast<uint8_t>();
					// fin.row(i) = (acc.block<1,3>(i,0)).cwiseMin(255.f).cwiseMax(0.f).cast<uint8_t>();
					// fin.row(i) = 255;

				// write
				WritableTile &outTile = outDset.blockingGetTileBufferForThread(0);
				encode(outTile.eimg, blendedImage);
				outTile.coord = BlockCoordinate{(uint64_t)level,(uint64_t)y,(uint64_t)x};
				outDset.sendCommand({DbCommand::TileReady, outTile.bufferIdx});

			}
		}


		// Finish up
		outDset.blockUntilEmptiedQueue();
		outDset.sendCommand(DbCommand{DbCommand::EndLvl, level});
		outDset.blockUntilEmptiedQueue();

		uint64_t finalTlbr[4];
		uint64_t nHere = outDset.determineLevelAABB(finalTlbr, level);
		uint64_t nExpected = (finalTlbr[2]-finalTlbr[0]) * (finalTlbr[3]-finalTlbr[1]);
		uint64_t tileTlbr[4] = { (uint64_t)tl(0), (uint64_t)tl(1), (uint64_t)br(0), (uint64_t)br(1) };
		uint64_t nExpectedInput = (1+tileTlbr[2]-tileTlbr[0]) * (1+tileTlbr[3]-tileTlbr[1]);
		printf(" - Final Tlbr on Lvl %d:\n", level);
		printf(" -        %lu %lu -> %lu %lu\n", finalTlbr[0], finalTlbr[1], finalTlbr[2], finalTlbr[3]);
		printf(" -        %lu tiles (%lu missing interior, %lu missing from input aoi)\n", nHere, nExpected-nHere, nExpectedInput-nHere);
		{
			printf(" - Recomputing Dataset Meta.\n");
			MDB_txn* txn;
			outDset.beginTxn(&txn, false);
			outDset.recompute_meta_and_write_slow(txn);
			outDset.endTxn(&txn);
		}


		return 0;
	}

}



int main(int argc, char** argv) {

	assert(argc == 2);

	ConvertParams cp;

	std::string inIndexList = argv[1];
	cp.idl = loadFilesBuildIndex(inIndexList, Image::Format::RGB);

	assert(cp.idl.nodes.size() > 0);

	cp.level = 16;

	if (false) {
		// TODO: If user specifies tlbr manually, should use that
	} else {
		Box2f box;
		for (auto &node : cp.idl.nodes) {
			box.extend(node.dset->tlbr_uwm.head<2>().cast<float>());
			box.extend(node.dset->tlbr_uwm.tail<2>().cast<float>());
		}
		cp.outputBox = box;
	}

	return run_it(cp);
}
