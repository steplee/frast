#pragma once

#include <Eigen/StdVector>
// #include "frastVk/core/buffer_utils.h"
// #include "frastVk/core/app.h"
// #include "frastVk/utils/eigen.h"
#include "frastVk/core/fvkApi.h"

using RowMatrix3d = Eigen::Matrix<double,3,3,Eigen::RowMajor>;
using RowMatrix4d = Eigen::Matrix<double,4,4,Eigen::RowMajor>;
using RowMatrix4f = Eigen::Matrix<float,4,4,Eigen::RowMajor>;

class FrustumSet {
	public:
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW

		// Build pipeline etc.
		FrustumSet(BaseApp* app, int nInSet);

		// void renderInPass(RenderState& rs, vk::CommandBuffer cmd);
		void render(RenderState& rs, Command& cmd);

		void setColor(int n, const float color[4]);
		void setIntrin(int n, float w, float h, float fx, float fy);
		void setPose(int n, const Eigen::Vector3d& pos, const RowMatrix3d& R, bool pushPath=true);
		void setNextPath(int n, const Vector4f& color);

		struct __attribute__((packed)) GlobalBuffer {
			float mvp[16];
			float model1[16];
			float model2[16];
		};


	private:

		static constexpr float near = 15.0   / 6378137.0;
		static constexpr float far  = 300.0 / 6378137.0;

		BaseApp* app;
		// BaseVkApp* app;
		// vk::raii::DescriptorPool descPool = {nullptr};
		// vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		// vk::raii::DescriptorSet globalDescSet = {nullptr};
		DescriptorSet globalDescSet;


		void init();

		GraphicsPipeline frustumPipeline;
		int nInSet;

		uint32_t nInds;
		ExBuffer inds;
		ExBuffer verts;
		// Holds viewProj then an array of model matrices
		ExBuffer globalBuffer;

		std::vector<double> modelMatrices;

		GraphicsPipeline pathPipeline;
		ExBuffer paths;
		int maxPaths = 8;
		int maxPathLen = 4096;
		std::vector<int> idToCurrentPath;
		std::vector<int> pathLens;
		std::vector<Vector4f> pathColors;
		std::vector<Vector4f> pathColorsById;
		int pathIdx = 0;

};


