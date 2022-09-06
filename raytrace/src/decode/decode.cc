#include "decode.h"

// #define STB_IMAGE_IMPLEMENTATION
// #include "stb_image.h"

#include "rt_decode.hpp"
#include <fmt/ostream.h>

LoadedInformation load_to(std::vector<Tile>& outs, const std::string& filename) {
	DecodedCpuTileData dtd;
	
	std::ifstream ifs { filename };
	decode_node_to_tile(ifs, dtd, true);

	// double I_SCALE = 1. / 6.378e6;
	double I_SCALE = 1.;

	RowMatrix4d SCALE_IT {RowMatrix4d::Identity()};
	SCALE_IT(0,0) = I_SCALE;
	SCALE_IT(1,1) = I_SCALE;
	SCALE_IT(2,2) = I_SCALE;

	Map<Matrix<double,4,4,ColMajor>> modelMat_ { dtd.modelMat };
	Matrix<double,4,4,ColMajor> modelMat = SCALE_IT * modelMat_;

	Vector3d ctr0 = modelMat.topLeftCorner<3,3>() * -Vector3d{128,128,128} + modelMat.topRightCorner<3,1>();

	// WARNING: Temp change...
	// modelMat.topRightCorner<3,1>().setZero();
	modelMat.topRightCorner<3,1>() = modelMat.topLeftCorner<3,3>() * -Vector3d{128,128,128};

	Matrix<double,4,4,RowMajor> inv_modelMat { modelMat.inverse() };
	fmt::print(" - Model Matrix:\n{}\n", modelMat);
	// fmt::print(" - Inv Model Matrix:\n{}\n", inv_modelMat);


	// Now convert to Tile
	for (auto& md : dtd.meshes) {
		Tile out;

		out.verts.allocateCpu(md.vert_buffer_cpu.size());
		out.indices.allocateCpu(md.ind_buffer_cpu.size());
		out.pixels.allocateCpu(md.texSize[0], md.texSize[1]);

		Array2f uvOff { md.uvOffset[0], md.uvOffset[1] };
		Array2f uvScale { md.uvScale[0], md.uvScale[1] };

		for (int i=0; i<md.vert_buffer_cpu.size(); i++) {
			auto &iv = md.vert_buffer_cpu[i];
			auto &ov = out.verts(i);
			Vector3d p =  (modelMat * Vector4d { (double)iv.x, (double)iv.y, (double)iv.z, 1 }).hnormalized();
			Vector3d n = (inv_modelMat * Vector4d { (double)iv.nx, (double)iv.ny, (double)iv.nz, 1}).hnormalized();

			// fmt::print(" - have p {}\n", p.transpose());

			ov.x = p(0);
			ov.y = p(1);
			ov.z = p(2);
			ov.nx = n(0);
			ov.ny = n(1);
			ov.nz = n(2);

			// vec2 uv_scale = cameraData.uvScaleAndOff[tileIndex].xy;
			// vec2 uv_off = cameraData.uvScaleAndOff[tileIndex].zw;
			// v_uv = (aUv + uv_off) * uv_scale * mask;
			Array2f uv = (Array2f{iv.u, iv.v} + uvOff) * uvScale;
			ov.u = uv(0);
			ov.v = uv(1);
		}

		for (int i=0; i<md.ind_buffer_cpu.size(); i+=3) {
			out.indices(i+0) = md.ind_buffer_cpu[i+0];
#if 1
			out.indices(i+1) = md.ind_buffer_cpu[i+1];
			out.indices(i+2) = md.ind_buffer_cpu[i+2];
#else
			out.indices(i+2) = md.ind_buffer_cpu[i+1];
			out.indices(i+1) = md.ind_buffer_cpu[i+2];
#endif
		}

		assert(md.texSize[2] == 4);
		for (int y=0; y<md.texSize[0]; y++) { // Just a memcpy
		for (int x=0; x<md.texSize[1]; x++) {
			Pixel& p = out.pixels(y,x);
			p.r = md.img_buffer_cpu[y*md.texSize[1]*md.texSize[2]+x*md.texSize[2]+0];
			p.g = md.img_buffer_cpu[y*md.texSize[1]*md.texSize[2]+x*md.texSize[2]+1];
			p.b = md.img_buffer_cpu[y*md.texSize[1]*md.texSize[2]+x*md.texSize[2]+2];
			p.rough1 = md.img_buffer_cpu[y*md.texSize[1]*md.texSize[2]+x*md.texSize[2]+3];
		}
		}

		outs.push_back(std::move(out));
	}


	int nv=0, ni=0;
	for (auto &ti : outs)
		nv += ti.verts.h, ni += ti.indices.h;
	fmt::print(" - Created {} meshes, with {} verts and {} inds\n", outs.size(), nv, ni);



	LoadedInformation info;

	info.model = modelMat;
	info.ctr0 = ctr0;
	info.ctr1 = modelMat.topLeftCorner<3,3>() * -Vector3d{128,128,128} + modelMat.topRightCorner<3,1>();

	{
		RowMatrix3d Ltp;
		Ltp.col(2) =  info.ctr0.normalized();
		Ltp.col(0) = -Ltp.col(2).cross(Vector3d::UnitZ()).normalized();
		Ltp.col(1) = -Ltp.col(0).cross(Ltp.col(2)).normalized();
		assert(Ltp.determinant() > .9999 and Ltp.determinant() < 1.00001);

		double height = (modelMat.topLeftCorner<3,3>() * Vector3d{256, 256, 256}).norm() * .5;
		Vector3d localEye { 0, 0, height };
		// Vector3d eye = Ltp * localEye + info.ctr1;
		Vector3d eye = Ltp * localEye;

		RowMatrix3d look; look <<
			1, 0, 0,
			0, -1, 0,
			0, 0, -1;

		info.recommendView.topLeftCorner<3,3>() = (Ltp * look).transpose();
		info.recommendView.topRightCorner<3,1>() = -info.recommendView.topLeftCorner<3,3>() * eye;
		info.recommendView.row(3) << 0,0,0,1;
		info.ltp = Ltp;
		fmt::print(" - Recommended View:\n{}\n", info.recommendView);
		fmt::print(" - Ctr0: {}\n", info.ctr0.transpose());
		fmt::print(" - Ctr1: {}\n", info.ctr1.transpose());
		fmt::print(" - Ltp:\n{}\n", Ltp);
	}

	return info;
}
