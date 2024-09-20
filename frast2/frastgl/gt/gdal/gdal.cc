#include "gdal.h"

#include "../gt_impl.hpp"

#include "../ftr/conversions.hpp"

#include <fmt/color.h>
#include <fmt/ostream.h>

#include "frast2/frastgl/shaders/ft.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

namespace frast {


template GtRenderer<GdalTypes, GdalTypes::Renderer>::GtRenderer(const typename GdalTypes::Config &cfg_);
template void GtRenderer<GdalTypes, GdalTypes::Renderer>::init(const AppConfig& cfg);
// template void GtRenderer<GdalTypes, GdalTypes::Renderer>::initDebugPipeline(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);

template void GtRenderer<GdalTypes, GdalTypes::Renderer>::update(GtUpdateContext<GdalTypes>&);
template void GtRenderer<GdalTypes, GdalTypes::Renderer>::defaultUpdate(Camera* cam);

// template void GtRenderer<GdalTypes, GdalTypes::Renderer>::render(RenderState&);
template void GtRenderer<GdalTypes, GdalTypes::Renderer>::renderDbg(RenderState&);

template GtPooledData<GdalTypes>::GtPooledData();

template void GtDataLoader<GdalTypes, GdalTypes::DataLoader>::pushAsk(GtAsk<GdalTypes>& ask);
template void GtDataLoader<GdalTypes, GdalTypes::DataLoader>::internalLoop();

static uint32_t log2_(uint32_t x) {
	uint32_t y = 0;
	while (x>>=1) y++;
	return y;
}

static bool isAncestorOf(const GdalCoordinate& ancestor, const GdalCoordinate& descendent) {
	// assert(false); // wrong.
	if (descendent.level() < ancestor.level()) return false;
	GdalCoordinate cur = descendent;
	// FIXME: inefficient
	while (cur.level() > ancestor.level()) cur = cur.parent();
	return ancestor.c == cur.c;
}

std::vector<GdalCoordinate> GdalCoordinate::enumerateChildren() const {
	std::vector<GdalCoordinate> cs;
	for (uint64_t i=0; i<4; i++) {
		uint64_t dx = i % 2;
		uint64_t dy = (i/2) % 2;
		cs.emplace_back(z()+1, y()*2+dx, x()*2+dy );
		// cs.emplace_back(z()-1, y()*2+dx, x()*2+dy );
		}
	return cs;
}


// NOTE: This is *overloading* the function defined in gt_impl.hpp (which worked with vulkan, but we require too much
// customization for the gl version and cannot use a generic method)
template<> void GtRenderer<GdalTypes, GdalTypes::Renderer>::render(RenderState& rs) {
	// fmt::print("my overload\n");
	if (cfg.allowCaster) {
		setCasterInRenderThread();
	}

	typename GdalTypes::RenderContext gtrc { rs, gtpd, casterStuff };

	// FIXME: Cache this in RenderState, this is very wasteful.
	float mvpf[16];
	rs.computeMvpf(mvpf);

	// FIXME: Do the two step xform with 'anchor', like rtr does!

	// WARNING: This is tricky!
	if (rs.camera and rs.camera->flipY_) glFrontFace(GL_CW);
	else glFrontFace(GL_CCW);
	// glFrontFace(GL_CW);
	glEnable(GL_CULL_FACE);

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



	glEnable(GL_TEXTURE_2D);


	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

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
		glUniform1i(1, 0); // sampler2d
		glActiveTexture(GL_TEXTURE0);

		glUniform1ui(3, casterStuff.casterMask); // mask
		glUniformMatrix4fv(4, 1, true, casterStuff.cpuCasterBuffer.casterMatrix1);
		glUniformMatrix4fv(5, 1, true, casterStuff.cpuCasterBuffer.casterMatrix2);

		glUniform4fv(6, 1, casterStuff.cpuCasterBuffer.color1);
		glUniform4fv(7, 1, casterStuff.cpuCasterBuffer.color2);

		// Eigen::Map<RowMatrix4f> m(casterStuff.cpuCasterBuffer.casterMatrix1);
		// fmt::print(" - caster matrix:\n{} mask {}\n", m, casterStuff.casterMask);
	}


	for (auto root : roots) {
		root->render(gtrc);
	}

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

GdalDataLoader::~GdalDataLoader() {
}

void GdalDataLoader::do_init() {
	for (const auto& colorPath : renderer->cfg.colorDsetPaths)
		colorDsets.push_back(std::make_unique<GdalDataset>(colorPath, false));

	elevDset = std::make_unique<GdalDataset>(renderer->cfg.elevDsetPath, true);
}

GdalDataLoader::GdalDataLoader() {
}

void GdalDataLoader::loadColor(GdalTile* tile, GdalTypes::DecodedCpuTileData::MeshData& mesh) {
	assert(renderer != nullptr);

	uint64_t tlbr[4] = { tile->coord.x(), tile->coord.y(), tile->coord.x()+1lu, tile->coord.y()+1lu };

	// TODO: Not the most efficient thing to loop through each...
	for (int i=0; i<colorDsets.size(); i++) {
		auto &colorDset = this->colorDsets[i];

		assert(colorDsets.size() == 1);
		// if (colorDsets.size() == 1 or colorDset->tileExists(tile->coord)) {
		if (1) {
			fmt::print(" - loading tile {} {} {}\n", tile->coord.z(), tile->coord.y(), tile->coord.x());
			// colorBuf = colorDset->getLocalTile(tile->coord.x(), tile->coord.y(), tile->coord.z());
			colorBuf = colorDset->getGlobalTile(tile->coord.x(), tile->coord.y(), tile->coord.z());


			if (colorBuf.type() == CV_8UC1) cv::cvtColor(colorBuf, colorBuf, cv::COLOR_GRAY2RGBA);
			if (colorBuf.type() == CV_8UC3) cv::cvtColor(colorBuf, colorBuf, cv::COLOR_RGB2RGBA);
			fmt::print(" - loading tile {} -> {}x{}x{}\n", tile->coord.c, colorBuf.rows, colorBuf.cols, colorBuf.channels());

			for (int i=3; i<colorBuf.total(); i+=4) {
				colorBuf.data[i] = 255;
			}
			assert(colorBuf.channels() == 4);
			// colorBuf = cv::Scalar{255,255,255,255};
			// cv::imshow("tile", colorBuf); cv::waitKey(30);

			cv::putText(colorBuf, fmt::format("g: {} {} z={}", tile->coord.x(), tile->coord.y(), tile->coord.z()), cv::Point{20,20}, 0, .6, cv::Scalar{0,255,0,255});
			int dz = colorDset->deepestLevelZ - tile->coord.z();
			int scale = 1 << dz;

			int lx = tile->coord.x() - colorDset->deepestLevelTlbr(0)/scale;
			// int ly = y - deepestLevelTlbr(1)/scale;
			// int ly = (colorDset->deepestLevelTlbr(3)+scale-1)/scale - tile->coord.y() - 1;
			// int ly = (colorDset->deepestLevelTlbr(3)-colorDset->deepestLevelTlbr(1)) / scale - (tile->coord.y() - colorDset->deepestLevelTlbr(1)/scale);
			// int ly = (colorDset->deepestLevelTlbr(3)-colorDset->deepestLevelTlbr(1)) / scale - (tile->coord.y() - colorDset->deepestLevelTlbr(3)/scale);
			int yy = tile->coord.y();
			// yy += colorDset->deepestLevelTlbr(3)/scale - colorDset->shallowestLevelTlbr(3) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ));
			// yy -= colorDset->deepestLevelTlbr(1)/scale - colorDset->shallowestLevelTlbr(1) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ));
			// if 
			// int ly = (colorDset->deepestLevelTlbr(3))/scale - yy - 1;
			// int ly = (colorDset->deepestLevelTlbr(3) + scale-1)/scale - yy - 1;
			int ly = ((colorDset->shallowestLevelTlbr(3)-1) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ))) - yy - 1;
			// ly += (colorDset->deepestLevelTlbr(3)+scale-1)/scale - colorDset->shallowestLevelTlbr(3) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ));
			// ly = - ly;
			// ly += (colorDset->deepestLevelTlbr(3)+scale-1)/scale - colorDset->shallowestLevelTlbr(3) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ));
			// ly += (colorDset->deepestLevelTlbr(3)+scale-1)/scale - colorDset->shallowestLevelTlbr(3) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ));
			
			lx += colorDset->deepestLevelTlbr(0)/scale - colorDset->shallowestLevelTlbr(0) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ));
			// ly += colorDset->deepestLevelTlbr(3)/scale - colorDset->shallowestLevelTlbr(3) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ));
			// ly += (colorDset->deepestLevelTlbr(3)+scale-1)/scale - colorDset->shallowestLevelTlbr(3) * (1 << (tile->coord.z() - colorDset->shallowestLevelZ));

			int ovr = dz;
			cv::putText(colorBuf, fmt::format("l: {} {} z={}", lx, ly, ovr), cv::Point{20,39}, 0, .5, cv::Scalar{0,155,155,255});

			auto size = colorBuf.total()*colorBuf.elemSize();
			mesh.img_buffer_cpu.resize(size);
			mesh.texSize[0] = colorBuf.rows;
			mesh.texSize[1] = colorBuf.cols;
			mesh.texSize[2] = colorBuf.channels();
			memcpy(mesh.img_buffer_cpu.data(), colorBuf.data, size);
			// cv::imshow("img", colorBuf); cv::waitKey(0);
			return;
		}

	}
}

void GdalDataLoader::loadElevAndMetaWithDted(GdalTile* tile, GdalTypes::DecodedCpuTileData::MeshData& mesh, const GdalTypes::Config& cfg) {
	uint64_t tz = tile->coord.z();
	uint64_t ty = tile->coord.y();
	uint64_t tx = tile->coord.x();
	assert(colorDsets.size() == 1);
	// Vector4d tlbrWm = colorDsets[0]->getLocalTileBoundsWm(tx,ty,tz);
	Vector4d tlbrWm = colorDsets[0]->getGlobalTileBoundsWm(tx,ty,tz);
	Vector4d tlbrUwm = tlbrWm / WebMercatorMapScale;

	// fmt::print(" - elev tlbr {} {} {} {}\n", tlbrWm[0], tlbrWm[1], tlbrWm[2], tlbrWm[3]);
	cv::Mat elevBuf(cfg.vertsAlongEdge, cfg.vertsAlongEdge, CV_16UC1);
	Vector4d elevTlbrWm = tlbrWm;
	elevTlbrWm(2) += (elevTlbrWm(2) - elevTlbrWm(0)) / (cfg.vertsAlongEdge);
	elevTlbrWm(3) += (elevTlbrWm(3) - elevTlbrWm(1)) / (cfg.vertsAlongEdge);
	elevDset->getWm(elevTlbrWm, elevBuf);
	// elevBuf.convertTo(elevBuf, CV_32FC1, 1./8.);

	int S = cfg.vertsAlongEdge;
	for (uint16_t yy=0; yy<cfg.vertsAlongEdge-1; yy++)
		for (uint16_t xx=0; xx<cfg.vertsAlongEdge-1; xx++) {
			uint16_t a = ((yy+0)*S) + (xx+0);
			uint16_t b = ((yy+0)*S) + (xx+1);
			uint16_t c = ((yy+1)*S) + (xx+1);
			uint16_t d = ((yy+1)*S) + (xx+0);
			// mesh.ind_buffer_cpu.push_back(b); mesh.ind_buffer_cpu.push_back(a); mesh.ind_buffer_cpu.push_back(c);
			// mesh.ind_buffer_cpu.push_back(d); mesh.ind_buffer_cpu.push_back(c); mesh.ind_buffer_cpu.push_back(a);
			mesh.ind_buffer_cpu.push_back(a); mesh.ind_buffer_cpu.push_back(b); mesh.ind_buffer_cpu.push_back(c);
			mesh.ind_buffer_cpu.push_back(c); mesh.ind_buffer_cpu.push_back(d); mesh.ind_buffer_cpu.push_back(a);
		}

	mesh.vert_buffer_cpu.resize(cfg.vertsAlongEdge*cfg.vertsAlongEdge);
	uint16_t* elevData = (uint16_t*) elevBuf.data;

	for (int32_t yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (int32_t xx=0; xx<cfg.vertsAlongEdge; xx++) {
			int32_t ii = ((cfg.vertsAlongEdge-1-yy)*cfg.vertsAlongEdge)+xx;
			// int32_t ii = ((yy)*cfg.vertsAlongEdge)+xx;

			float xxx_ = static_cast<float>(xx) / static_cast<float>(cfg.vertsAlongEdge-1);
			float yyy_ = static_cast<float>(yy) / static_cast<float>(cfg.vertsAlongEdge-1);
			float xxx = xxx_ * tlbrUwm(2) + (1-xxx_) * tlbrUwm(0);
			// float yyy = yyy_ * tlbrUwm(3) + (1-yyy_) * tlbrUwm(1);
			float yyy = yyy_ * tlbrUwm(1) + (1-yyy_) * tlbrUwm(3);

			float zzz = (elevData[(yy)*elevBuf.cols+xx] / 8.0) / WebMercatorMapScale;
			// zzz = 0;

			mesh.vert_buffer_cpu[ii].x = xxx;
			mesh.vert_buffer_cpu[ii].y = yyy;
			mesh.vert_buffer_cpu[ii].z = zzz;
			mesh.vert_buffer_cpu[ii].w = 0;
			mesh.vert_buffer_cpu[ii].u = xxx_;
			mesh.vert_buffer_cpu[ii].v = yyy_;

			// fmt::print(" - vert {} {} {} | {} {}\n", xxx,yyy,zzz, xxx_,yyy_);
		}

	unit_wm_to_ecef((float*)mesh.vert_buffer_cpu.data(), mesh.vert_buffer_cpu.size(), (float*)mesh.vert_buffer_cpu.data(), 6);
}

GdalRenderer::~GdalRenderer() {
}

// TODO: Note: I've moved also the 'upload' code in here since it is easier (initially meant to split it up)
int GdalDataLoader::loadTile(GdalTile* tile, GdalTypes::DecodedCpuTileData& dtd, bool isOpen) {

	loadColor(tile, dtd.mesh);
	if (elevDset)
		loadElevAndMetaWithDted(tile, dtd.mesh, renderer->cfg);
	else {
		assert(false);
	}

	uint32_t total_meshes = 1;
	std::vector<uint32_t> gatheredIds(total_meshes);
	renderer->gtpd.withdraw(gatheredIds, !isOpen);
	// renderer.gtpd.withdraw(gatheredIds, false);

	auto &coord = tile->coord;
	int mesh_ii = 0;
	tile->meshIds.push_back(gatheredIds[0]);

	// TODO: Is this a good approx?
	// tile->geoError = (4.0f / 255.f) / (1 << tile->coord.level());

	//////////////////////////////////
	// Upload
	//////////////////////////////////

	// fmt::print(fmt::fg(fmt::color::magenta), " - uploading\n");
	{
		int meshId = tile->meshIds[0];
		auto &td = renderer->gtpd.datas[meshId];

		// See if we must re-allocate larger buffers.
		// If the texture needs a re-allocation, we must mark that the DescSet needs to be updated too
		// NOTE: This will not happen in ftr (but does in rt)
		auto vert_size = sizeof(GdalPackedVertex)*dtd.mesh.vert_buffer_cpu.size();
		auto indx_size = sizeof(uint16_t)*dtd.mesh.ind_buffer_cpu.size();
		auto img_size  = sizeof(uint8_t)*dtd.mesh.img_buffer_cpu.size();

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

	}

	// uploader.execute();
	// FIXME: blocking synch

	// fmt::print(fmt::fg(fmt::color::dark_gray), " - [#loader] loaded {} ({} meshes)\n", tile->coord.toString(), total_meshes);
	// tile->loaded = true;

	return total_meshes;
}

//
// NOTE: Unlike in frast1, in frast2 this is **called in the render thread**.
//
bool GdalTile::upload(GdalTypes::DecodedCpuTileData& dctd, int idx, GtTileData& td) {
	assert(idx == 0);
	// Upload vertices, indices, texture

	{
		// int meshId = tile->meshIds[0];
		// auto &td = renderer.gtpd.datas[meshId];

		// See if we must re-allocate larger buffers.
		// If the texture needs a re-allocation, we must mark that the DescSet needs to be updated too
		// NOTE: This will not happen in ftr (but does in rt)
		auto vert_size = sizeof(GdalPackedVertex)*dctd.mesh.vert_buffer_cpu.size();
		auto indx_size = sizeof(uint16_t)*dctd.mesh.ind_buffer_cpu.size();
		auto img_size  = sizeof(uint8_t)*dctd.mesh.img_buffer_cpu.size();

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
		glBufferData(GL_ARRAY_BUFFER, vert_size, dctd.mesh.vert_buffer_cpu.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, td.inds);
		// glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indx_size, dctd.mesh.ind_buffer_cpu.data());
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indx_size, dctd.mesh.ind_buffer_cpu.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		glBindTexture(GL_TEXTURE_2D, td.tex);
		// fmt::print(" - upload {} {} {}({}x{}x{})\n", vert_size, indx_size, img_size, dctd.mesh.texSize[0],dctd.mesh.texSize[1],dctd.mesh.texSize[2]);
		auto fmt = dctd.mesh.texSize[2] == 4 ? GL_RGBA : dctd.mesh.texSize[2] == 3 ? GL_RGB : GL_LUMINANCE;
		// FIXME: Allocate with texstorage & then use subimage2d
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dctd.mesh.texSize[1], dctd.mesh.texSize[0], 0, fmt, GL_UNSIGNED_BYTE, dctd.mesh.img_buffer_cpu.data());
		glBindTexture(GL_TEXTURE_2D, 0);

		td.residentInds = dctd.mesh.ind_buffer_cpu.size();
		td.residentVerts = dctd.mesh.vert_buffer_cpu.size();
	}

	return false;
}

void GdalTile::doRenderCasted(GtTileData& td, const CasterStuff& casterStuff, int meshIdx) {
	// fmt::print(" - rendering casted\n");

	glBindTexture(GL_TEXTURE_2D, td.tex);
	glBindBuffer(GL_ARRAY_BUFFER, td.verts);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, td.inds);

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*6, 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*6, (void*)(4*4));

	glDrawElements(GL_TRIANGLES, td.residentInds, GL_UNSIGNED_SHORT, 0);
}

void GdalTile::doRender(GtTileData& td, int meshIdx) {

	// NOTE: To avoid excessive state switches and line noise: assume correct shader is already bound (as well as uniforms)

	glBindTexture(GL_TEXTURE_2D, td.tex);
	glBindBuffer(GL_ARRAY_BUFFER, td.verts);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, td.inds);

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*6, 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*6, (void*)(4*4));

	// fmt::print("(rendering {} inds)\n", td.residentInds);
	glDrawElements(GL_TRIANGLES, td.residentInds, GL_UNSIGNED_SHORT, 0);
	// glDrawElements(GL_POINTS, td.residentInds, GL_UNSIGNED_SHORT, 0);
	// glDrawArrays(GL_POINTS, 0, 2);

}

void GdalRenderer::initPipelinesAndDescriptors(const AppConfig& cfg) {

	// FIXME: Replace

	// TODO: 1) Compile Shaders

	normalShader = Shader{ft_tile_vsrc, ft_tile_fsrc};

	if (this->cfg.allowCaster) {
		do_init_caster_stuff();
		castableShader = Shader{ft_tile_casted_vsrc, ft_tile_casted_fsrc};
	}

	/*
	
		std::vector<GraphicsPipeline*> enabledPipelines;
		enabledPipelines.push_back(&gfxPipeline);
		if (this->cfg.allowCaster) enabledPipelines.push_back(&casterStuff.pipeline);

		assert(not loadShader(*device,
				gfxPipeline.vs,
				gfxPipeline.fs,
				"ftr/ftr1"));

		float viewportXYWH[4] = {
			0,0,
			(float)cfg.width,
			(float)cfg.height
		};
		PipelineBuilder builder;
		builder.depthTest = cfg.depth;

		VertexInputDescription vid;

		// uint32_t    location; uint32_t    binding; VkFormat    format; uint32_t    offset;
		VkVertexInputAttributeDescription attrPos  { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 };
		VkVertexInputAttributeDescription attrUv   { 1, 0, VK_FORMAT_R32G32_SFLOAT,   4*4 };
		// VkVertexInputAttributeDescription attrNrml { 2, 0, VK_FORMAT_R8G8B8A8_USCALED, 8 };
		vid.attributes = { attrPos, attrUv };
		vid.bindings = { VkVertexInputBindingDescription { 0, sizeof(FtPackedVertex), VK_VERTEX_INPUT_RATE_VERTEX } };

		// Create buffers for each tile

		if (FtTypes::PushConstants::Enabled)
			gfxPipeline.pushConstants.push_back(VkPushConstantRange{
					VK_SHADER_STAGE_VERTEX_BIT,
					0,
					sizeof(FtPushConstants)});

		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, gfxPipeline.vs, gfxPipeline.fs);

		// Create descriptors
		// There are two (or three) descriptor sets
		//    Set0: GlobalData
		//          binding0: UBO holding MVP as well as an array of model matrices etc. for each tile
		//    Set1: TileData
		//          binding0: Array of image textures
		//    Set2: CasterData
		//          (TODO)

		// GlobalData
		uint32_t globalDataBindingIdx = gtpd.globalDescSet.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		gtpd.globalDescSet.create(dpool, enabledPipelines);

		// TileData
		uint32_t tileImgsBindingIdx = gtpd.tileDescSet.addBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, GT_NUM_TILES, VK_SHADER_STAGE_FRAGMENT_BIT);
		gtpd.tileDescSet.create(dpool, enabledPipelines);

		// mat44_map = (float*) globalDataBuffer.map();
		// for (int i=0; i<16; i++) mat44_map[i] = (i%5) == 0;

		// Create pipeline

		gfxPipeline.create(*device, viewportXYWH, builder, srp, 0);

		// Create per-tile buffers and fill tile datas
		gtpd.datas.resize(GT_NUM_TILES);
		for (int i=0; i<GT_NUM_TILES; i++) {
			auto &td = gtpd.datas[i];
			td.tex.set(VkExtent2D{256,256}, VK_FORMAT_R8G8B8A8_UNORM, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			td.verts.set(2*2048*sizeof(FtPackedVertex),
					// VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
			td.inds.set(2*2048*sizeof(uint16_t),
					// VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
			td.tex.create(*device);
			td.verts.create(*device);
			td.inds.create(*device);
		}

		// Write with buffers

		// GlobalData
		VkDescriptorBufferInfo bufferInfo { gtpd.globalBuffer, 0, sizeof(FtTypes::GlobalBuffer) };
		gtpd.globalDescSet.update(VkWriteDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
				gtpd.globalDescSet, 0,
				0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				nullptr, &bufferInfo, nullptr
				});

		// TileData
		std::vector<VkDescriptorImageInfo> imgInfos(GT_NUM_TILES);
		for (int i=0; i<GT_NUM_TILES; i++) imgInfos[i] = VkDescriptorImageInfo { sampler, gtpd.datas[i].tex.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
		gtpd.tileDescSet.update(VkWriteDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
				gtpd.tileDescSet, 0,
				0, GT_NUM_TILES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				imgInfos.data(), nullptr, nullptr
				});


		// Transition images to correct format

		Barriers barriers;
		for (int i=0; i<GT_NUM_TILES; i++) {
			barriers.append(gtpd.datas[i].tex, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
		}
		cmd.begin().barriers(barriers).end();
		Fence fence { *device };
		Submission submission { { *device, q } };
		submission.fence = fence;
		submission.submit(&cmd.cmdBuf, 1, true);



		// Setup caster stuff

		if (this->cfg.allowCaster) {
			do_init_caster_stuff(*device, 2, dpool);

			PipelineBuilder builder;
			builder.depthTest = cfg.depth;
			loadShader(*device,
						casterStuff.pipeline.vs,
						casterStuff.pipeline.fs,
						"ftr/cast");


			if (FtPushConstants::Enabled)
				casterStuff.pipeline.pushConstants.push_back(VkPushConstantRange{
						VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(FtPushConstants)});

			// fmt::print(" - Caster pipeline has {} descriptor layouts\n", casterStuff.pipeline.setLayouts.size());

			builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, casterStuff.pipeline.vs, casterStuff.pipeline.fs);
			casterStuff.pipeline.create(*device, viewportXYWH, builder, srp, 0);
		}

		*/


}

}
