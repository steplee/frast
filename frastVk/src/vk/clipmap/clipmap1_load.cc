#include "clipmap1.h"
#include <iostream>


Loader::Loader() {}

void Loader::init(ClipMapConfig cfg_, std::string colorDsetPath, std::string terrainDsetPath)
{
	this->cfg = cfg_;

	{
		DatasetReaderOptions dopts {};
		colorDsets.push_back(std::make_shared<DatasetReader>(colorDsetPath, dopts));
	}
	{
		DatasetReaderOptions dopts {};
		terrainDsets.push_back(std::make_shared<DatasetReader>(terrainDsetPath, dopts));
	}


	LoadedMultiLevelData& out = loadedData;
	for (int i=0; i<cfg.levels; i++) {
		std::cout << " - pushing with szs " << (int)cfg.pixelsAlongEdge << " " << (int)cfg.vertsPerLevel << "\n";
		out.colorImages.push_back(Image{(int)cfg.pixelsAlongEdge, (int)cfg.pixelsAlongEdge, Image::Format::RGBA});
		out.colorImages.back().alloc();
		out.altImages.push_back(Image{(int)cfg.vertsPerLevel, (int)cfg.vertsPerLevel, Image::Format::TERRAIN_2x8});
		out.altImages.back().alloc();
		std::cout << " - imgs: " 
			<< out.colorImages.back().w << " "
			<< out.colorImages.back().h << ", "
			<< out.altImages.back().w << " "
			<< out.altImages.back().h << "\n";
	}
}

void Loader::load(const Ask& ask) {
	LoadedMultiLevelData& out = loadedData;

	double unit_pos[3] = {
		static_cast<double>(ask.pos[0]) * .5 + .5,
		static_cast<double>(ask.pos[1]) * .5 + .5,
		static_cast<double>(ask.pos[2]) * .5 + .5};
	double wm_pos[3] = {
		static_cast<double>(ask.pos[0]) * 20037508.342789248,
		static_cast<double>(ask.pos[1]) * 20037508.342789248,
		static_cast<double>(ask.pos[2]) * 20037508.342789248 };

	int deepestLevel = 17;
	int lvl_idx = 0;
	for (int lvl = deepestLevel; lvl>deepestLevel-cfg.levels; lvl--) {

		uint64_t lvlCtr[2] = {
			static_cast<uint64_t>(unit_pos[0] / (1<<lvl)),
			static_cast<uint64_t>(unit_pos[1] / (1<<lvl)) };

		uint64_t lvlTlbr[4] = {
			lvlCtr[0] - 1, lvlCtr[1] - 1,
			lvlCtr[0] + 2, lvlCtr[1] + 2 };

		std::cout << " - fetch at "
			<< ask.pos[0] << " " << ask.pos[1]
			<< " with imgs: "
			<< out.colorImages[lvl_idx].w << " "
			<< out.colorImages[lvl_idx].h << ", "
			<< out.altImages[lvl_idx].w << " "
			<< out.altImages[lvl_idx].h << "\n";

		//int fetchBlocks(Image& out, uint64_t lvl, const uint64_t tlbr[4], MDB_txn* txn0);
		colorDsets[0]->fetchBlocks(out.colorImages[lvl_idx], lvl, lvlTlbr, nullptr);


		lvl_idx++;
	}

	out.ctr_x = ask.pos[0];
	out.ctr_y = ask.pos[1];
}
