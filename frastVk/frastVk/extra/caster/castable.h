#pragma once

#include <frast/image.h>
#include "frastVk/core/buffer_utils.h"
#include "frastVk/core/app.h"

// This is not allocated on the CPU, but is useful to access mapped data instead of the void*
struct __attribute__((packed)) CasterBuffer {
	float casterMatrix[2*16];
	uint32_t casterMask;
};

namespace rt { class RtRenderer; }
namespace tr2 { class TiledRenderer; }
class Castable;

struct CasterWaitingData {
	friend class rt::RtRenderer;
	friend class tr2::TiledRenderer;
	friend class Castable;

	public:
		inline void setMatrix1(float* m) { memcpy(casterMatrix1, m, sizeof(float)*16); haveMatrix1 = true; }
		inline void setMatrix2(float* m) { memcpy(casterMatrix2, m, sizeof(float)*16); haveMatrix2 = true; }
		inline void setImage(const Image& image_) { image = image_; }
		inline void setMask(const uint32_t mask_) { mask = mask_; }

	private:
		Image image;
		float casterMatrix1[16];
		float casterMatrix2[16];
		uint32_t mask = 0;
		bool haveMatrix1=false, haveMatrix2=false;
};

struct CasterStuff {
	uint32_t casterMask; // should match the gpu buffer variable.
	bool casterTextureSet = false; // true after first time set
	ResidentImage casterImages[1];
	ResidentBuffer casterBuffer;
	PipelineStuff casterPipelineStuff;
	vk::raii::DescriptorPool casterDescPool = {nullptr};
	vk::raii::DescriptorSetLayout casterDescSetLayout = {nullptr};
	vk::raii::DescriptorSet casterDescSet = {nullptr};
};

/*
 *
 *
 *   Castable
 *
 *   Holds data for casting. A user can subclass this. It is not fully ready-to-go, the subclass
 *   must create the pipeline after calling do_init_caster_stuff
 *
 *   TODO: This should use CRTP so we can access the 'app' member of the subclass, rather
 *   than having to provide it...
 *
 *
 */

class Castable {
	public:
		void setCasterInRenderThread(const CasterWaitingData& cwd, BaseVkApp* app);
		void do_init_caster_stuff(BaseVkApp* app);

	protected:
		CasterStuff casterStuff;

};
