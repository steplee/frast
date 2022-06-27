#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <cassert>

class BaseVkApp;

uint64_t scalarSizeOfFormat(const vk::Format& f);

//__attribute__((packed)) struct VertexPUN {
	//float x,y,z, u,v, nx,ny,nz;
//};

struct VertexInputDescription {
	std::vector<vk::VertexInputBindingDescription> bindings;
	std::vector<vk::VertexInputAttributeDescription> attributes;

	vk::PipelineVertexInputStateCreateFlags flags;
};


struct __attribute__((packed, aligned(16)))
MeshPushContants {
	alignas(16) float model[16];
};

class ExImage;

struct QueueDeviceSpec {
	vk::raii::Queue& q;
	vk::raii::Device& dev;
	vk::raii::PhysicalDevice& pdev;
	std::vector<uint32_t> queueFamilyIndices;
};

struct ExBuffer {
	vk::raii::Buffer buffer { nullptr };
	vk::raii::BufferView view { nullptr };
	vk::raii::DeviceMemory mem { nullptr };

	void* map();
	void unmap();
	void* mappedAddr = nullptr;

	// Buffer details
	uint64_t givenSize = 0;
	uint64_t residentSize = 0;
	vk::SharingMode sharingMode = vk::SharingMode::eExclusive;
	vk::BufferUsageFlags usageFlags;
	// Memory details: easy way to upload is to mapMemory, which requires host visible.
	vk::MemoryPropertyFlags memPropFlags;

	// If mappable, make sure it's host accessible. Otherwise its device local, but eTransferDst.
	void setAsBuffer(uint64_t len, bool mappable, vk::Flags<vk::BufferUsageFlagBits> usage);
	// void create(vk::raii::Device& d, const vk::PhysicalDevice& pd, const std::vector<uint32_t>& queueFamilyIndices);
	void create(QueueDeviceSpec& spec);
	void uploadNow(void* cpuData, uint64_t len, uint64_t offset=0);

	//void copyFromImage(ResidentImage& other);
	bool copyFromImage(const vk::CommandBuffer &copyCmd, const vk::Image& srcImg,  vk::ImageLayout prevLayout, const vk::Device& d, const vk::Queue& q, const vk::Fence* fence,
			const vk::Semaphore* waitSema, const vk::Semaphore* signalSema,
			vk::Extent3D ex, vk::Offset3D off={},
			vk::ImageAspectFlagBits aspect=vk::ImageAspectFlagBits::eColor);

};

struct Uploader {
	//vk::Queue q;
	QueueDeviceSpec qds;
	vk::raii::Fence fence { nullptr };
	vk::raii::CommandPool pool { nullptr };
	vk::raii::CommandBuffer cmd { nullptr };

	BaseVkApp* app = nullptr;


	inline Uploader(
			QueueDeviceSpec &qds_, vk::BufferUsageFlags scratchFlags_ = vk::BufferUsageFlagBits::eTransferSrc) : qds(qds_), scratchFlags(scratchFlags_) {
	}
	Uploader(BaseVkApp* app, QueueDeviceSpec& qds);

	Uploader(const Uploader&) = delete;
	Uploader& operator=(const Uploader&) = delete;
	inline Uploader(Uploader&& o) : qds(std::move(o.qds)) {
		fence = std::move(o.fence), pool = std::move(o.pool), cmd = std::move(o.cmd);
		app = o.app;
	}
	inline Uploader& operator=(Uploader&& o) = delete;

	vk::BufferUsageFlags scratchFlags;
	ExBuffer scratchBuffer;
	void uploadScratch(void* data, size_t len);

	void uploadSync(ExBuffer& buffer, void *data, uint64_t len, uint64_t off);
	void uploadSync(ExImage& image, void *data, uint64_t len, uint64_t off);
};

struct MeshDescription {
	enum class ScalarType {
		Float, Double, UInt8, UInt16, UInt32,
		UInt8_scaled, UInt16_scaled
	};
	static inline uint typeToSize(const ScalarType& s) {
		switch (s) {
			case ScalarType::Float: return 4;
			case ScalarType::Double: return 8;
			case ScalarType::UInt32: return 4;
			case ScalarType::UInt8_scaled:
			case ScalarType::UInt8: return 1;
			case ScalarType::UInt16_scaled:
			case ScalarType::UInt16: return 2;
		};
		throw std::runtime_error("unk type");
	}

	ScalarType posType=ScalarType::Float,
			   normalType=ScalarType::Float,
			   uvType=ScalarType::Float;

	uint8_t posDims = 3;
	uint64_t rows=0, rowSize=0, ninds=0;
	bool haveUvs = false;
	bool haveNormals = false;
	vk::IndexType indType;

	VertexInputDescription getVertexDescription();
};

/*
// Must call fill(), then createAndUpload().
// fill() will copy planar vertex data to packed vertex data.
// Destructor or calling freeCpu() will delete cpu copied data.
struct ResidentMesh : public MeshDescription {
	using IndType = uint32_t;

	inline ResidentMesh() {
		indType = vk::IndexType::eUint32;
	}

	//Eigen::Matrix<float,-1,-1,Eigen::RowMajor> verts;
	//Eigen::Matrix<IndType,-1,1> inds;
	float* verts = nullptr;
	IndType* inds = nullptr;
	~ResidentMesh();

	ResidentBuffer vertBuffer;
	ResidentBuffer indBuffer;

	MeshPushContants pushConstants;

	inline uint64_t size() const {
		return rows*rowSize;
	}
	inline uint64_t sizeInds() const {
		return ninds * sizeof(IndType);
	}

	void createAndUpload(vk::raii::Device& d, const vk::PhysicalDevice& pd, const std::vector<uint32_t>& queueFamilyIndices);
	void createAndUpload(Uploader& uploader);

	void fill(
			int posDims, const std::vector<float>& verts,
			const std::vector<float>& uvs,
			const std::vector<float>& normals,
			const std::vector<IndType>& inds);
	void freeCpu();

};
*/



struct ImageBarrierDetails {};

/*
 * The view and sampler may or may not exist, depending on
 * which createAs*() func is called.
 *
 */
struct ExImage {
	vk::raii::Image image { nullptr };
	vk::raii::DeviceMemory mem { nullptr };
	vk::raii::ImageView view { nullptr };
	vk::raii::Sampler sampler { nullptr };

	// Image details
	vk::Format format;
	vk::Format viewFormat = vk::Format::eUndefined;
	vk::ImageUsageFlags usageFlags;
	vk::Extent3D extent;
	// Memory details
	vk::MemoryPropertyFlags memPropFlags;
	// ImageView details
	vk::ImageAspectFlags aspectFlags;
	// Passed to sampler.
	bool unnormalizedCoordinates = false;

	inline uint32_t channels() const {
		return scalarSizeOfFormat(format);
	}
	inline uint64_t size() const {
		return scalarSizeOfFormat(format) * extent.width * extent.height * extent.depth;
	}

	void createAsDepthBuffer(Uploader& uploader, int h, int w, bool cpuVisible=false, vk::ImageUsageFlagBits extraFlags={});
	void createAsTexture(Uploader& uploader, int h, int w, vk::Format f, uint8_t* data, vk::ImageUsageFlags extraFlags={}, vk::SamplerAddressMode=vk::SamplerAddressMode::eClampToEdge);
	void createAsStorage(vk::raii::Device& dev, vk::raii::PhysicalDevice& pdev, int h, int w, vk::Format f, vk::ImageUsageFlags extraFlags={}, vk::SamplerAddressMode=vk::SamplerAddressMode::eClampToEdge);
	void createAsCpuVisible(Uploader& uploader, int h, int w, vk::Format f, uint8_t* data, vk::ImageUsageFlags extraFlags={}, vk::SamplerAddressMode=vk::SamplerAddressMode::eClampToEdge);
	void create_(Uploader& uploader);

	bool copyFrom(const vk::CommandBuffer &copyCmd, const vk::Image& srcImg,  vk::ImageLayout prevLayout, const vk::Device& d, const vk::Queue& q, const vk::Fence* fence,
			const vk::Semaphore* waitSema, const vk::Semaphore* signalSema,
			vk::Extent3D ex, vk::Offset3D off={},
			vk::ImageAspectFlagBits aspect=vk::ImageAspectFlagBits::eColor);

	vk::ImageMemoryBarrier barrierTo(vk::ImageLayout to, const ImageBarrierDetails& details={});

	vk::ImageLayout layout;
};

