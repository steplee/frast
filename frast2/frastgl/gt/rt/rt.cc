#include "rt.h"
#include "../gt_impl.hpp"
#include "frast2/frastgl/core/shader.h"

#include "decode/rt_decode.hpp"

#include <fmt/color.h>
#include <fmt/ostream.h>

#include "frast2/frastgl/shaders/rt.h"

// Just need to instantiate the templated functions here.
// TODO Have a macro do this in gt_impl.hpp, given for example "RtTypes"

namespace frast {

template GtRenderer<RtTypes, RtTypes::Renderer>::GtRenderer(const typename RtTypes::Config &cfg_);
template void GtRenderer<RtTypes, RtTypes::Renderer>::init(const AppConfig& cfg);

template void GtRenderer<RtTypes, RtTypes::Renderer>::update(GtUpdateContext<RtTypes>&);
template void GtRenderer<RtTypes, RtTypes::Renderer>::defaultUpdate(Camera* cam);
// template void GtRenderer<RtTypes, RtTypes::Renderer>::render(RenderState&);
template void GtRenderer<RtTypes, RtTypes::Renderer>::renderDbg(RenderState&);

template GtPooledData<RtTypes>::GtPooledData();

template void GtDataLoader<RtTypes, RtTypes::DataLoader>::pushAsk(GtAsk<RtTypes>& ask);
template void GtDataLoader<RtTypes, RtTypes::DataLoader>::internalLoop();

std::vector<RtCoordinate> RtCoordinate::enumerateChildren() const {
	std::vector<RtCoordinate> cs;
	for (int i=0; i<8; i++) {
		cs.emplace_back(*this, i+'0');
		return cs;
	}
	return cs;
}

RtRenderer::~RtRenderer() {
}

int RtDataLoader::loadTile(RtTile* tile, RtTypes::DecodedCpuTileData& dtd, bool isOpen) {
	// Read file from disk
	// Parse protos
	// Unpack to cpu buffer

	// usleep(10'000);
	// return 0;

	std::string fname = renderer.cfg.rootDir + "/node/" + std::string{tile->coord.key, (size_t)tile->coord.level()};
	std::ifstream ifs(fname);


	// fmt::print(" - Decoding {}\n", fname);
	if (rt::decode_node_to_tile(ifs, dtd, false)) {
		fmt::print(fmt::fg(fmt::color::orange), " - [#loadTile] decode '{}' failed, skipping tile.\n", fname);
		// tile->loaded = true;
		// return dtd.meshes.size();
	}

	if (dtd.meshes.size() == 0) {
		// tile->loaded = true; // loaded, but empty
		return 0;
	}

	uint32_t total_meshes = dtd.meshes.size();
	std::vector<uint32_t> gatheredIds(total_meshes);
	renderer.gtpd.withdraw(gatheredIds, !isOpen);
	// renderer.gtpd.withdraw(gatheredIds, false);

	auto &coord = tile->coord;
	int mesh_ii = 0;
	for (auto &meshData : dtd.meshes) {
		uint32_t idx = gatheredIds[mesh_ii++];
		tile->meshIds.push_back(idx);
		// auto error = static_cast<Derived*>(this)->uploadTile(tile, meshData, renderer.gtpd.datas[idx]);
		// auto error = tile->upload(meshData, renderer.gtpd.datas[idx]);
		// assert(not error);
	}

	// fmt::print(fmt::fg(fmt::color::dark_gray), " - [#loader] loaded {}\n", tile->coord.toString());
	// if (dtd.meshes.size() > 1) fmt::print(fmt::fg(fmt::color::dark_gray), " - [#loader] loaded {} ({} meshes)\n", tile->coord.toString(), dtd.meshes.size());
	tile->loaded = true;

	Eigen::Map<Eigen::Matrix4d> mm(dtd.modelMat);
	constexpr double R1         = (6378137.0);
	constexpr double R1i = 1.0 / R1;
	Eigen::Matrix4d scale_m; scale_m <<
		R1i, 0, 0, 0,
		0, R1i, 0, 0,
		0, 0, R1i, 0,
		0, 0, 0, 1;
	mm = scale_m * mm;

	for (int i=0; i<16; i++) tile->model = mm.cast<float>();

	// This needs to be known in the constructor, not here after loading!
	/*
	if (dtd.metersPerTexel <= 0) {
		// TODO: Is this a good approx?
		// dtd.metersPerTexel = 2 * R1 / (1 << tile->nc.level());
		// tile->geoError = 4.0 * (1/255.) / (1 << tile->nc.level());
		// tile->geoError = 8.0 * (1/255.) / (1 << tile->coord.level());
		tile->geoError = (8.0f / 255.f) / (1 << tile->coord.level());
		// tile->geoError = 16.0 * (1/255.) / (1 << tile->nc.level());
		// fmt::print(" - [{}/{}] geoError from lvl {}\n", tile->coord.key, tile->coord.level(), tile->geoError);
	} else {
		// tile->geoError = dtd.metersPerTexel / (255. * R1);
		tile->geoError = dtd.metersPerTexel / (R1);
		// fmt::print(" - [{}/{}] geoError from mpt {}\n", tile->coord.key, tile->coord.level(), tile->geoError);
	}
	*/



	return total_meshes;
}

bool RtTile::upload(RtTypes::DecodedCpuTileData& dctd, int idx, GtTileData& td) {
	// Upload vertices, indices, texture

	//////////////////////////////////
	// Upload
	//////////////////////////////////

	// fmt::print(fmt::fg(fmt::color::magenta), " - uploading\n");
	// tile->uvInfos.resize(tile->meshIds.size()*4);
	// for (int i=0; i<tile->meshIds.size(); i++) {
		// int meshId = tile->meshIds[i];
		// auto& mesh = dctd.meshes[i];
		// auto &td = renderer.gtpd.datas[meshId];
	auto& mesh = dctd.meshes[idx];

		// See if we must re-allocate larger buffers.
		// If the texture needs a re-allocation, we must mark that the DescSet needs to be updated too
		auto vert_size = sizeof(RtPackedVertex)*mesh.vert_buffer_cpu.size();
		auto indx_size = sizeof(uint16_t)*mesh.ind_buffer_cpu.size();
		auto img_size  = sizeof(uint8_t)*mesh.img_buffer_cpu.size();

		if (uvInfos.size() < (idx+1)*4) uvInfos.resize((1+idx)*4);
		uvInfos[idx*4+0] = mesh.uvScale[0];
		uvInfos[idx*4+1] = mesh.uvScale[1];
		uvInfos[idx*4+2] = mesh.uvOffset[0];
		uvInfos[idx*4+3] = mesh.uvOffset[1];

		/*
		if (vert_size > td.verts.capacity_) {
			td.verts.deallocate();
			td.verts.set(vert_size * 2,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
			td.verts.create(*uploader.device);
		}
		if (indx_size > td.inds.capacity_) {
			td.inds.deallocate();
			td.inds.set(indx_size * 2,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
			td.inds.create(*uploader.device);
		}
		// TODO: Plenty of optimizations to do here
		// if (img_size > td.tex.capacity_) {
		if (td.tex.extent.width != dtd.meshes[i].texSize[1] or td.tex.extent.height != dtd.meshes[i].texSize[0]) {
			// fmt::print(" - Recreating texture ({} / {}) (wh {} {})\n", img_size, td.tex.capacity_, dtd.meshes[i].texSize[0], dtd.meshes[i].texSize[1]);
			td.mustWriteImageDesc = true;
			td.texOld = std::move(td.tex);
			td.tex.deallocate();
			td.tex.set(VkExtent2D{dtd.meshes[i].texSize[1], dtd.meshes[i].texSize[0]}, VK_FORMAT_R8G8B8A8_UNORM, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			td.tex.create(*uploader.device);
		}

		uploader.enqueueUpload(td.verts, dtd.meshes[i].vert_buffer_cpu.data(), vert_size, 0);
		uploader.enqueueUpload(td.inds, dtd.meshes[i].ind_buffer_cpu.data(), indx_size, 0);
		uploader.enqueueUpload(td.tex, dtd.meshes[i].img_buffer_cpu.data(), img_size, 0, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		tile->uvInfos[i*4+0] = dtd.meshes[i].uvScale[0];
		tile->uvInfos[i*4+1] = dtd.meshes[i].uvScale[1];
		tile->uvInfos[i*4+2] = dtd.meshes[i].uvOffset[0];
		tile->uvInfos[i*4+3] = dtd.meshes[i].uvOffset[1];

		td.residentInds = dtd.meshes[i].ind_buffer_cpu.size();
		td.residentVerts = dtd.meshes[i].vert_buffer_cpu.size();
		*/

		// FIXME: REPLACE
		/*
		if (vert_size > td.verts.capacity_) {
			td.verts.deallocate();
			td.verts.set(vert_size * 2,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
			td.verts.create(*uploader.device);
		}
		if (indx_size > td.inds.capacity_) {
			td.inds.deallocate();
			td.inds.set(indx_size * 2,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
			td.inds.create(*uploader.device);
		}

		uploader.enqueueUpload(td.verts, dtd.mesh.vert_buffer_cpu.data(), vert_size, 0);
		uploader.enqueueUpload(td.inds, dtd.mesh.ind_buffer_cpu.data(), indx_size, 0);
		uploader.enqueueUpload(td.tex, dtd.mesh.img_buffer_cpu.data(), img_size, 0, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
		*/

		// FIXME: Use SubDAta once allocated once.
		glBindBuffer(GL_ARRAY_BUFFER, td.verts);
		// glBufferSubData(GL_ARRAY_BUFFER, 0, vert_size, dctd.mesh.vert_buffer_cpu.data());
		glBufferData(GL_ARRAY_BUFFER, vert_size, mesh.vert_buffer_cpu.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, td.inds);
		// glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indx_size, mesh.ind_buffer_cpu.data());
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indx_size, mesh.ind_buffer_cpu.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		glBindTexture(GL_TEXTURE_2D, td.tex);
		// fmt::print(" - upload {} {} {}({}x{}x{})\n", vert_size, indx_size, img_size, mesh.texSize[0],mesh.texSize[1],mesh.texSize[2]);
		auto fmt = mesh.texSize[2] == 4 ? GL_RGBA : mesh.texSize[2] == 3 ? GL_RGB : GL_LUMINANCE;
		// FIXME: Allocate with texstorage & then use subimage2d
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mesh.texSize[1], mesh.texSize[0], 0, fmt, GL_UNSIGNED_BYTE, mesh.img_buffer_cpu.data());
		glBindTexture(GL_TEXTURE_2D, 0);

		td.residentInds = mesh.ind_buffer_cpu.size();
		td.residentVerts = mesh.vert_buffer_cpu.size();
	// }

	// fmt::print(fmt::fg(fmt::color::magenta), " - done uploading\n");

	return false;
}

void RtTile::doRenderCasted(GtTileData& td, const CasterStuff& casterStuff, int meshIdx) {
	// fmt::print(" - rendering casted\n");

	glUniformMatrix4fv(3, 1, false, this->model.data());
	glUniform4fv(4, 1, &this->uvInfos[meshIdx*4]);

	glBindTexture(GL_TEXTURE_2D, td.tex);
	glBindBuffer(GL_ARRAY_BUFFER, td.verts);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, td.inds);

	// glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*6, 0);
	// glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*6, (void*)(4*4));
	glVertexAttribPointer(0, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(RtPackedVertex), 0);
	glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(RtPackedVertex), (void*)(4));
	glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(RtPackedVertex), (void*)(8));

	glDrawElements(GL_TRIANGLE_STRIP, td.residentInds, GL_UNSIGNED_SHORT, 0);
}

void RtTile::doRender(GtTileData& td, int meshIdx) {

	// NOTE: To avoid excessive state switches and line noise: assume correct shader is already bound (as well as uniforms)

	glUniformMatrix4fv(3, 1, false, this->model.data());
	glUniform4fv(4, 1, &this->uvInfos[meshIdx*4]);

	glBindTexture(GL_TEXTURE_2D, td.tex);
	glBindBuffer(GL_ARRAY_BUFFER, td.verts);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, td.inds);

	// glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*6, 0);
	// glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*6, (void*)(4*4));
	glVertexAttribPointer(0, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(RtPackedVertex), 0);
	glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(RtPackedVertex), (void*)(4));
	glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(RtPackedVertex), (void*)(8));

	// fmt::print("(rendering {} inds)\n", td.residentInds);
	glDrawElements(GL_TRIANGLE_STRIP, td.residentInds, GL_UNSIGNED_SHORT, 0);

}


// NOTE: This is *overloading* the function defined in gt_impl.hpp (which worked with vulkan, but we require too much
// customization for the gl version and cannot use a generic method)
template<> void GtRenderer<RtTypes, RtTypes::Renderer>::render(RenderState& rs) {
	// fmt::print("my overload\n");
	if (cfg.allowCaster) {
		setCasterInRenderThread();
	}

	typename RtTypes::RenderContext gtrc { rs, gtpd, casterStuff };

	/*
	float mvpf[16];
	rs.mvpf(mvpf);
	*/

	// Shift vertices by the negative eye vector before multiplying by the MVP.
	// This helps avoid float32 precision problems and make things render *MUCH* nicer.
	// It decomposes the scale+rotation+translation into a two step (translation, scale+rotation) process,
	// that allows the scale+rotation part to operate on smaller numbers.
	double mvpd[16];
	Vector3d offset;
	rs.copyEye(offset.data());
	rs.computeMvp(mvpd);
	Vector4f anchor;
	anchor.head<3>() = offset.cast<float>();
	anchor(3) = 0;
	RowMatrix4d shift_(RowMatrix4d::Identity()); shift_.topRightCorner<3,1>() = anchor.head<3>().cast<double>();
	Eigen::Map<const RowMatrix4d> mvp_ { mvpd };
	RowMatrix4f new_mvp = (mvp_ * shift_).cast<float>();

	// WARNING: This is tricky!
	if (rs.camera and rs.camera->flipY_) glFrontFace(GL_CW);
	else glFrontFace(GL_CCW);
	glEnable(GL_CULL_FACE);

	/*
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	float mvpf_column[16];
	for (int i=0; i<4; i++)
	for (int j=0; j<4; j++) mvpf_column[i*4+j] = mvpf[j*4+i];
	glLoadMatrixf(mvpf_column);
	glBegin(GL_LINES);
	glColor4f(1,0,0,1); glVertex3f(0,0,0); glVertex3f(1,0,0);
	glColor4f(0,1,0,1); glVertex3f(0,0,0); glVertex3f(0,1,0);
	glColor4f(0,0,1,1); glVertex3f(0,0,0); glVertex3f(0,0,1);
	glEnd();
	*/

	glEnable(GL_TEXTURE_2D);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	if (casterStuff.casterMask == 0) {
		// Normal shader.
		glUseProgram(normalShader.prog);

		// glUniformMatrix4fv(0, 1, true, mvpf); // mvp
		glUniformMatrix4fv(0, 1, true, new_mvp.data()); // mvp
		glUniform4fv(1, 1, anchor.data()); // anchor

		glUniform1i(2, 0); // sampler2d
		// glUniformMatrix4fv(2, 1, true, mvpf); // model: different for each tile
		// glUniformMatrix4fv(3, 1, true, mvpf); // uvScaleOff: different for each tile

		glActiveTexture(GL_TEXTURE0);
	} else {
		// Castable shader.
		glUseProgram(castableShader.prog);

		// glUniformMatrix4fv(0, 1, true, mvpf); // mvp
		glUniformMatrix4fv(0, 1, true, new_mvp.data()); // mvp
		glUniform4fv(1, 1, anchor.data()); // anchor

		glUniform1i(2, 0); // sampler2d
		// glUniformMatrix4fv(2, 1, true, mvpf); // model: different for each tile
		// glUniformMatrix4fv(3, 1, true, mvpf); // uvScaleOff: different for each tile

		// Caster tex
		glUniform1i(5, 1);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, casterStuff.tex);
		// Regular tex
		glUniform1i(2, 0); // sampler2d
		glActiveTexture(GL_TEXTURE0);
		// <tex different each tile>

		glUniform1ui(6, casterStuff.casterMask); // mask
		glUniformMatrix4fv(7, 1, true, casterStuff.cpuCasterBuffer.casterMatrix1);
		glUniformMatrix4fv(8, 1, true, casterStuff.cpuCasterBuffer.casterMatrix2);
	}

	for (auto root : roots) {
		root->render(gtrc);
	}


	/*

	if (casterStuff.casterMask == 0) {
		// Normal shader.
		glUseProgram(normalShader.prog);
		glUniformMatrix4fv(0, 1, true, mvpf); // mvp

		glUniform1i(1, 0); // sampler2d
		glActiveTexture(GL_TEXTURE0);
	} else {
		// Casted shader.
		glUseProgram(castableShader.prog);
		glUniformMatrix4fv(0, 1, true, mvpf); // mvp

		glUniform1i(2, 1);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, casterStuff.tex);
		glUniform1i(2, 0); // sampler2d
		glActiveTexture(GL_TEXTURE0);

		glUniform1ui(3, casterStuff.casterMask); // mask
		glUniformMatrix4fv(4, 1, true, casterStuff.cpuCasterBuffer.casterMatrix1);
		glUniformMatrix4fv(5, 1, true, casterStuff.cpuCasterBuffer.casterMatrix2);

		Eigen::Map<RowMatrix4f> m(casterStuff.cpuCasterBuffer.casterMatrix1);
		// fmt::print(" - caster matrix:\n{} mask {}\n", m, casterStuff.casterMask);
	}


	for (auto root : roots) {
		root->render(gtrc);
	}
	*/

	glUseProgram(0);
	glDisable(GL_TEXTURE_2D);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);


	if (GT_DEBUG and debugMode) renderDbg(rs);
}

// void RtTile::updateGlobalBuffer(RtTypes::GlobalBuffer* gpuBuffer) { }

void RtRenderer::initPipelinesAndDescriptors(const AppConfig& cfg) {

	normalShader = std::move(Shader{rt_tile_vsrc, rt_tile_fsrc});
	fmt::print(" - normalShader :: {}\n", normalShader.prog);

	if (this->cfg.allowCaster) {
		do_init_caster_stuff();
		castableShader = std::move(Shader{rt_tile_casted_vsrc, rt_tile_casted_fsrc});
		fmt::print(" - castableShader :: {}\n", castableShader.prog);
	}

}





}
