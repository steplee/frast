#pragma once

#include "vk/buffer_utils.h"
#include "vk/app.h"
#include <thread>
#include <mutex>
#include <condition_variable>

#include <frast/db.h>
#include <frast/image.h>

/*
 *
 * A simple first implementation of the clipmap.
 *
 * There are two sets of data: (one inactive) and (one waiting to load or loading).
 * One the new data is loaded, it is swapped on the next render.
 *
 * I don't try to minimize total number of tris drawn or anything like that.
 * I don't try to re-use data by doing complicated blitting. Buffers are just recreated and copied.
 * I don't (at first) even do frustum culling.
 *
 * The same indices and XY positions are used for all calls.
 * Only textures and Zs change with new loaded data.
 *
 * I will use either a uniform or storage buffer for the Z values.
 * Note: A texture is not used for the Z values because they are being read from
 *       the vertex shader and I'd rather pay the price of sampling up front during loading,
 *       then on every frame doing texture sampling.
 *
 * Note: During loading, multiple levels' boundaries are smoothed to prevent seams.
 *
 *       e.g. in  a1----------------------a2
 *                b1------b2------b3------b4
 *
 *      a1 = b1 and a2 = b4 should be true, *but*
 *      b2 and b3 must be moved to lie on the line joining a1-a2.
 *      But that would be disruptive, so smooth both boundaries (move both b *and* a).
 *      Theoretically, this could be done in the vertex shader, if okay to take many samples...
 *      Note: as long as a tile has multiple inner vertices not connected to inside and outside,
 *            the correct contours will be followed
 *		Note: consider adding more triangles at boundaries, to make smoothing better
 *
 * Note: the new mesh/task shader would actually be very nice here since it would
 *       be easy to create vertices from where we are currently looking, and avoid
 *       an actual culling step!
 *
 *
 * |-------------------------|
 * |                         |
 * |                         |
 * |      |-----------|      |
 * |      |    ---    |      |
 * |      |    | |    |      |
 * |      |    ---    |      |
 * |      |-----------|      |
 * |                         |
 * |                         |
 * |-------------------------|
 *
 */


struct Ask {
	alignas(16) double pos[3];
	void setFromCamera(Camera* cam);
};

struct ClipMapConfig {
	constexpr static uint32_t maxLevel = 22;
	uint32_t levels;
	uint32_t pixelsPerTile;
	uint32_t pixelsAlongEdge;
	uint32_t tilesPerLevel;
	uint32_t vertsPerTileEdge;
	uint32_t vertsAlongEdge;
	uint32_t vertsPerLevel;
	float expansion;

	inline ClipMapConfig() {}
	ClipMapConfig(int levels, int tilesPerLevel, int pixPer, int vertsPer)
		: levels(levels), tilesPerLevel(tilesPerLevel), pixelsPerTile(pixPer), vertsPerTileEdge(vertsPer)
	{
		assert(tilesPerLevel == 2 or tilesPerLevel == 4);
		expansion = tilesPerLevel == 4 ? 2.f : 3.f;
		pixelsAlongEdge = tilesPerLevel * pixelsPerTile;
		vertsAlongEdge = vertsPerTileEdge * tilesPerLevel + 1;
		vertsPerLevel = vertsAlongEdge * vertsAlongEdge;
	}
};

struct LoadedMultiLevelData {
	std::vector<Image> colorImages;
	std::vector<Image> altImages;
	double ctr_x, ctr_y;
};

class DatasetReader;
struct Loader {
	Loader();
	void init(ClipMapConfig cfg, std::string colorDsetPath, std::string terrainDsetPath);

	ClipMapConfig cfg;
	LoadedMultiLevelData loadedData;

	std::vector<std::shared_ptr<DatasetReader>> colorDsets;
	std::vector<std::shared_ptr<DatasetReader>> terrainDsets;
	void load(const Ask& ask);
};


class ClipMapRenderer1 {
	private:

		struct MultiLevelData {
			// There will be as many of these as framebuffers:
			std::vector<vk::raii::CommandBuffer> cmdBufs;
			// There will be as many of these levels:
			std::vector<ResidentImage> images;
			std::vector<ResidentBuffer> altBufs;

			vk::raii::DescriptorPool descPool { nullptr };
			vk::raii::DescriptorSet descSet { nullptr };

			double ctr_x, ctr_y;
		};

		struct __attribute__((packed, aligned(4))) MldPushConstants {
			uint32_t lvlOffset;
			uint32_t numLevels;
			float expansion;
		};

		vk::raii::DescriptorSetLayout mldDescSetLayout { nullptr };
		ResidentMesh mldMesh;
		PipelineStuff mldPipelineStuff;
		std::vector<int> lvlIndOffset;

		vk::raii::DescriptorSetLayout globalDescLayout { nullptr };
		vk::raii::DescriptorSet globalDescSet { nullptr };
		ResidentBuffer camAndMetaBuffer;

		vk::raii::Queue cmUploadQueue { nullptr };
		Uploader cmUploader;
		Loader dataLoader;

	public:

		ClipMapRenderer1(VkApp* app_);
		~ClipMapRenderer1();

		vk::CommandBuffer stepAndRender(RenderState& rs, FrameData& fd, Camera* cam);
		vk::CommandBuffer render(RenderState& rs, FrameData& fd, Camera* cam);

		void init();

	protected:
		ClipMapConfig cfg;
		VkApp* app = nullptr;


		// Order matters for destructor order: pool decl must be ABOVE cmdBufs
		vk::raii::CommandPool commandPool { nullptr };
		MultiLevelData mlds[2];
		int dataReadIdx = 0;
		int dataWriteIdx = 0;

		std::thread loaderThread;
		void loaderLoop();



		std::mutex askMtx;
		std::condition_variable askCv;
		Ask currentAsk;
		bool haveAsk = false;
		bool haveNewData = false;
		bool doStop_ = false;

	private:
};

