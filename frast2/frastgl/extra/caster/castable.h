#pragma once


// NOTE: This is copied from the version designed for Vulkan, so the types here
// don't make much sense and are overcomplicated.

#include <opencv2/core.hpp>

#include <GL/glew.h>
#include <GL/gl.h>

namespace frast {

// This is not allocated on the CPU, but is useful to access mapped data instead of the void*
struct __attribute__((packed)) CasterBuffer {
	float casterMatrix1[16];
	float casterMatrix2[16];
	float color1[4] = {1,0,0,1};
	float color2[4] = {0,0,1,1};
	uint32_t casterMask;
};

// namespace rt { class RtRenderer; }
// namespace tr2 { class TiledRenderer; }
class FtRenderer;
class RtRenderer;
class Castable;

// CPU resident data, that can be modified off the render thread :: TODO: add mutex
struct CasterWaitingData {
	// friend class rt::RtRenderer;
	// friend class tr2::TiledRenderer;
	friend class Castable;
	friend class FtRenderer;
	friend class RtRenderer;

	public:
		inline void setMatrix1(float* m) { memcpy(casterMatrix1, m, sizeof(float)*16); haveMatrix1 = true; }
		inline void setMatrix2(float* m) { memcpy(casterMatrix2, m, sizeof(float)*16); haveMatrix2 = true; }
		inline void setImage(const cv::Mat& image_) { imgIsNew = true; image = image_; }
		inline void setMask(const uint32_t mask_) { mask = mask_; }
		inline void setColor1(float* c) { memcpy(color1, c, 4*4); haveColor1 = true; }
		inline void setColor2(float* c) { memcpy(color2, c, 4*4); haveColor2 = true; }

		inline bool isNew() const {
			return imgIsNew or haveMatrix1 or haveMatrix2 or haveColor1 or haveColor2;
		}
		inline void setNotNew() {
			imgIsNew = haveColor1 = haveColor2 = haveMatrix2 = haveMatrix1 = false;
		}

	private:

		cv::Mat image;


		float casterMatrix1[16];
		float casterMatrix2[16];
		float color1[4];
		float color2[4];
		uint32_t mask = 0;
		bool haveMatrix1=false, haveMatrix2=false;
		bool haveColor1=false, haveColor2=false;
		bool imgIsNew = false;
};

// GPU Resident data, should only be touched on render thread.
// TODO Note: This is not taking full use of Vulkan's asynchroncity, but that is okay for now
struct CasterStuff {
	uint32_t casterMask=0; // should match the gpu buffer variable.
	bool casterTextureSet = false; // true after first time set

	CasterBuffer cpuCasterBuffer;
	GLuint tex=0;
	uint32_t lastTexSize = 0;


	// ExImage casterImage;
	// ExBuffer casterBuffer;
	// GraphicsPipeline pipeline;
	// DescriptorSet dset;

	// vk::raii::DescriptorPool casterDescPool = {nullptr};
	// vk::raii::DescriptorSetLayout casterDescSetLayout = {nullptr};
	// vk::raii::DescriptorSet casterDescSet = {nullptr};
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
		// void setCasterInRenderThread(CasterWaitingData& cwd);
		void setCasterInRenderThread();
		// void do_init_caster_stuff(Device& device, uint32_t queueNumberForUploader, TheDescriptorPool& dpool);
		void do_init_caster_stuff();
		~Castable();

	public:
		CasterWaitingData cwd;
	protected:
		CasterStuff casterStuff;
		// Device* device { nullptr };

		// Queue queue;
		// Sampler sampler;
		// ExUploader uploader;

};

}
