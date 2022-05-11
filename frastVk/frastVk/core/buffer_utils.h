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

class ResidentImage;

struct ResidentBuffer {
	vk::raii::Buffer buffer { nullptr };
	vk::raii::BufferView view { nullptr };
	vk::raii::DeviceMemory mem { nullptr };



	// Buffer details
	uint64_t givenSize = 0;
	uint64_t residentSize = 0;
	vk::SharingMode sharingMode = vk::SharingMode::eExclusive;
	vk::BufferUsageFlags usageFlags;
	// Memory details: easy way to upload is to mapMemory, which requires host visible.
	vk::MemoryPropertyFlagBits memPropFlags;
	// BufferView details : TODO
	//vk::ImageAspectFlags aspectFlags;

	// If mappable, make sure it's host accessible. Otherwise its device local, but eTransferDst.
	void setAsVertexBuffer(uint64_t len, bool mappable=false, vk::BufferUsageFlags extraFlags={});
	void setAsIndexBuffer(uint64_t len, bool mappable=false);
	void setAsUniformBuffer(uint64_t len, bool mappable=false);
	void setAsOtherBuffer(uint64_t len, bool mappable=false);
	void setAsStorageBuffer(uint64_t len, bool mappable=false);
	void setAsBuffer(uint64_t len, bool mappable, vk::Flags<vk::BufferUsageFlagBits> usage);
	void create(vk::raii::Device& d, const vk::PhysicalDevice& pd, const std::vector<uint32_t>& queueFamilyIndices);
	void upload(void* cpuData, uint64_t len, uint64_t offset=0);

};

struct Uploader {
	vk::Queue q;
	vk::raii::Fence fence { nullptr };
	vk::raii::CommandPool pool { nullptr };
	vk::raii::CommandBuffer cmd { nullptr };

	BaseVkApp* app = nullptr;

	inline Uploader() {}
	Uploader(BaseVkApp* app, vk::Queue q_);

	Uploader(const Uploader&) = delete;
	Uploader& operator=(const Uploader&) = delete;
	inline Uploader(Uploader&& o) {
		fence = std::move(o.fence), pool = std::move(o.pool), cmd = std::move(o.cmd);
		app = o.app;
		q = o.q;
	}
	inline Uploader& operator=(Uploader&& o) {
		fence = std::move(o.fence), pool = std::move(o.pool), cmd = std::move(o.cmd);
		app = o.app;
		q = o.q;
		return *this;
	}
	ResidentBuffer scratchBuffer;
	void uploadScratch(void* data, size_t len);

	void uploadSync(ResidentBuffer& buffer, void *data, uint64_t len, uint64_t off);
	void uploadSync(ResidentImage& image, void *data, uint64_t len, uint64_t off);
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




/*
 * The view and sampler may or may not exist, depending on
 * which createAs*() func is called.
 *
 */
struct ResidentImage {
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
	vk::MemoryPropertyFlagBits memPropFlags = vk::MemoryPropertyFlagBits::eHostVisible;
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

	void createAsDepthBuffer(Uploader& uploader, int h, int w);
	void createAsTexture(Uploader& uploader, int h, int w, vk::Format f, uint8_t* data, vk::ImageUsageFlags extraFlags={}, vk::SamplerAddressMode=vk::SamplerAddressMode::eClampToEdge);
	void createAsCpuVisible(Uploader& uploader, int h, int w, vk::Format f, uint8_t* data, vk::ImageUsageFlags extraFlags={}, vk::SamplerAddressMode=vk::SamplerAddressMode::eClampToEdge);
	void create_(Uploader& uploader);
};

