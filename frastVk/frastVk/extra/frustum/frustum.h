#pragma once

#include <Eigen/StdVector>
#include "frastVk/core/buffer_utils.h"
#include "frastVk/core/app.h"
// #include "frastVk/utils/eigen.h"

using RowMatrix3d = Eigen::Matrix<double,3,3,Eigen::RowMajor>;
using RowMatrix4d = Eigen::Matrix<double,4,4,Eigen::RowMajor>;
using RowMatrix4f = Eigen::Matrix<float,4,4,Eigen::RowMajor>;

class FrustumSet {
	public:
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW

		// Build pipeline etc.
		FrustumSet(BaseVkApp* app, int nInSet);

		void renderInPass(RenderState& rs, vk::CommandBuffer cmd);

		void setColor(int n, const float color[4]);
		void setIntrin(int n, float w, float h, float fx, float fy);
		void setPose(int n, const Eigen::Vector3d& pos, const RowMatrix3d& R, bool pushPath=true);
		void setNextPath(int n, const Vector4f& color);


	private:

		static constexpr float near = 5.0   / 6378137.0;
		static constexpr float far  = 200.0 / 6378137.0;
		BaseVkApp* app;

		vk::raii::DescriptorPool descPool = {nullptr};
		vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		vk::raii::DescriptorSet globalDescSet = {nullptr};


		void init();
		PipelineStuff pipelineStuff;
		int nInSet;

		uint32_t nInds;
		ResidentBuffer inds;
		ResidentBuffer verts;
		// Holds viewProj then an array of model matrices
		ResidentBuffer matrices;

		std::vector<double> modelMatrices;

		PipelineStuff pathPipelineStuff;
		ResidentBuffer paths;
		int maxPaths = 8;
		int maxPathLen = 4096;
		std::vector<int> idToCurrentPath;
		std::vector<int> pathLens;
		std::vector<Vector4f> pathColors;
		std::vector<Vector4f> pathColorsById;
		int pathIdx = 0;

};


