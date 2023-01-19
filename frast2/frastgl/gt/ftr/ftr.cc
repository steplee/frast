
#include "ftr.h"
#include "../gt_impl.hpp"
// #include "frastVk/core/fvkShaders.h"

#include "conversions.hpp"

#include <fmt/color.h>
#include <fmt/ostream.h>

#include "frast2/frastgl/shaders/ft.h"

#include <opencv2/highgui.hpp>


namespace frast {

// Just need to instantiate the templated functions here.
// TODO Have a macro do this in gt_impl.hpp, given for example "FtTypes"

template GtRenderer<FtTypes, FtTypes::Renderer>::GtRenderer(const typename FtTypes::Config &cfg_);
template void GtRenderer<FtTypes, FtTypes::Renderer>::init(const AppConfig& cfg);
// template void GtRenderer<FtTypes, FtTypes::Renderer>::initDebugPipeline(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);

template void GtRenderer<FtTypes, FtTypes::Renderer>::update(GtUpdateContext<FtTypes>&);
template void GtRenderer<FtTypes, FtTypes::Renderer>::defaultUpdate(Camera* cam);

// template void GtRenderer<FtTypes, FtTypes::Renderer>::render(RenderState&);
template void GtRenderer<FtTypes, FtTypes::Renderer>::renderDbg(RenderState&);

template GtPooledData<FtTypes>::GtPooledData();

template void GtDataLoader<FtTypes, FtTypes::DataLoader>::pushAsk(GtAsk<FtTypes>& ask);
template void GtDataLoader<FtTypes, FtTypes::DataLoader>::internalLoop();

static uint32_t log2_(uint32_t x) {
	uint32_t y = 0;
	while (x>>=1) y++;
	return y;
}

static bool isAncestorOf(const FtCoordinate& ancestor, const FtCoordinate& descendent) {
	if (descendent.level() < ancestor.level()) return false;
	FtCoordinate cur = descendent;
	// FIXME: inefficient
	while (cur.level() > ancestor.level()) cur = cur.parent();
	return ancestor.c == cur.c;
}

std::vector<FtCoordinate> FtCoordinate::enumerateChildren() const {
	std::vector<FtCoordinate> cs;
	for (uint64_t i=0; i<4; i++) {
		uint64_t dx = i % 2;
		uint64_t dy = (i/2) % 2;
		cs.emplace_back(z()+1, y()*2+dx, x()*2+dy );
		}
	return cs;
}




// NOTE: This is *overloading* the function defined in gt_impl.hpp (which worked with vulkan, but we require too much
// customization for the gl version and cannot use a generic method)
template<> void GtRenderer<FtTypes, FtTypes::Renderer>::render(RenderState& rs) {
	// fmt::print("my overload\n");
	if (cfg.allowCaster) {
		setCasterInRenderThread();
	}

	typename FtTypes::RenderContext gtrc { rs, gtpd, casterStuff };

	float mvpf[16];
	rs.mvpf(mvpf);

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

		Eigen::Map<RowMatrix4f> m(casterStuff.cpuCasterBuffer.casterMatrix1);
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






FtDataLoader::~FtDataLoader() {
	for (auto cd : colorDsets) delete cd;
	if (elevDset) delete elevDset;
}

FtDataLoader::FtDataLoader(typename FtTypes::Renderer& renderer_) : GtDataLoader<FtTypes, FtDataLoader>(renderer_) {

	EnvOptions opt;
	for (const auto& colorPath : renderer.cfg.colorDsetPaths)
		colorDsets.push_back(new FlatReaderCached(colorPath, opt));

	// FIXME: Disabled
	EnvOptions opt1;
	opt1.isTerrain = true;
	if (renderer.cfg.elevDsetPath.length() > 1)
		elevDset = new FlatReaderCached(renderer.cfg.elevDsetPath, opt1);


	/*
	DatasetReaderOptions dopts1, dopts2;
	dopts1.allowInflate = false;
	dopts2.allowInflate = true;

	// if (renderer.cfg.obbIndexPaths.size() != renderer.cfg.colorDsetPaths.size())

	if (renderer.cfg.elevDsetPath.length())
		elevDset  = new DatasetReader(renderer.cfg.elevDsetPath,  dopts2);
	if (elevDset)
		elevDset->beginTxn(&elev_txn, true);

	for (const auto& dsetPath : renderer.cfg.colorDsetPaths) {
		auto colorDset = new DatasetReader(dsetPath, dopts1);
		MDB_txn* color_txn;
		colorDset->beginTxn(&color_txn, true);
		color_txns.push_back(color_txn);
		assert(renderer.cfg.texSize == colorDset->tileSize());
		colorDsets.push_back(colorDset);
	}

	renderer.colorFormat = Image::Format::RGBA;
	// if (colorDset->format() == Image::Format::GRAY or cfg.channels == 1) renderer.colorFormat = Image::Format::GRAY;
	colorBuf = Image { (int)colorDsets[0]->tileSize(), (int)colorDsets[0]->tileSize(), renderer.colorFormat };
	colorBuf.alloc();
	if (renderer.colorFormat == Image::Format::RGBA)
		for (int y=0; y<renderer.cfg.texSize; y++)
		for (int x=0; x<renderer.cfg.texSize; x++)
			colorBuf.buffer[y*renderer.cfg.texSize*4+x*4+3] = 255;

	if (elevDset) {
		elevBuf = Image { (int)elevDset->tileSize(), (int)elevDset->tileSize(), Image::Format::TERRAIN_2x8 };
		elevBuf.alloc();
		memset(elevBuf.buffer, 0, elevBuf.size());
	}
	*/

}

void FtDataLoader::loadColor(FtTile* tile, FtTypes::DecodedCpuTileData::MeshData& mesh) {
	auto& tex = renderer.gtpd.datas[tile->meshIds[0]].tex;

	uint64_t tlbr[4] = { tile->coord.x(), tile->coord.y(), tile->coord.x()+1lu, tile->coord.y()+1lu };

	// TODO: Not the most efficient thing to loop through each...
	for (int i=0; i<colorDsets.size(); i++) {
		auto colorDset = this->colorDsets[i];

		// FIXME: HERE
		/*
		if (colorDsets.size() == 1 or colorDset->tileExists(tile->coord, color_txns[i])) {
			int n_missing = colorDset->fetchBlocks(colorBuf, tile->coord.z(), tlbr, color_txns[i]);
			// fmt::print(" - [#loadColor()] fetched tiles from tlbr {} {} {} {} ({} missing)\n", tlbr[0],tlbr[1],tlbr[2],tlbr[3], n_missing);
			// if (n_missing > 0) return true;
			if (n_missing == 0) {
				mesh.img_buffer_cpu.resize(colorBuf.size());
				memcpy(mesh.img_buffer_cpu.data(), colorBuf.buffer, colorBuf.size());
				return;
			}
		}
		*/

		if (colorDsets.size() == 1 or colorDset->tileExists(tile->coord)) {
			// fmt::print(" - loading tile {} {} {}\n", tile->coord.z(), tile->coord.y(), tile->coord.x());
			colorBuf = colorDset->getTile(tile->coord, 4);
			// fmt::print(" - loading tile {} -> {}x{}x{}\n", tile->coord.c, colorBuf.rows, colorBuf.cols, colorBuf.channels());
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


void FtDataLoader::loadElevAndMetaWithDted(FtTile* tile, FtTypes::DecodedCpuTileData::MeshData& mesh, const FtTypes::Config& cfg) {
	uint64_t tz = tile->coord.z();
	uint64_t ty = tile->coord.y();
	uint64_t tx = tile->coord.x();
	float lvlScale = 2. / (1 << tz);
	float ox = tx * lvlScale * 1. - 1.;
	float oy = (ty * lvlScale * 1. - 1.);

	/*
	// double bboxWm[4] = {
			// ox * WebMercatorMapScale,
			// oy * WebMercatorMapScale,
			// (ox+lvlScale) * WebMercatorMapScale,
			// (oy+lvlScale) * WebMercatorMapScale,
	// };
	// bool failure = elevDset->rasterIo(elevBuf, bboxWm);

	uint16_t res_ratio = cfg.texSize / cfg.vertsAlongEdge; // 32 for now
	uint32_t lvlOffset = log2_(res_ratio);
	// fmt::print(" - res_ratio {}, o {}\n", res_ratio, lvlOffset);
	uint64_t z = tz - lvlOffset;
	uint64_t y = ty >> lvlOffset;
	uint64_t x = tx >> lvlOffset;
	uint64_t tlbr[4] = { x,y, x+1lu,y+1lu };

	// FIXME: REPLACE
	int n_missing = elevDset->fetchBlocks(elevBuf, z, tlbr, elev_txn);

	if (n_missing > 0) {
		memset(elevBuf.buffer, 0, elevBuf.size());
	}
	*/

	// elevBuf.create(cfg.vertsAlongEdge, cfg.vertsAlongEdge, CV_16UC1);
	// cv::Mat rasterIo(double tlbr[4], int w, int h, int c);
	double tlbrWm[4] = {
			ox * WebMercatorMapScale,
			oy * WebMercatorMapScale,
			(ox+lvlScale) * WebMercatorMapScale,
			(oy+lvlScale) * WebMercatorMapScale,
	};
	// fmt::print(" - elev tlbr {} {} {} {}\n", tlbrWm[0], tlbrWm[1], tlbrWm[2], tlbrWm[3]);
	elevBuf = elevDset->rasterIo(tlbrWm, cfg.vertsAlongEdge, cfg.vertsAlongEdge, 1);

	int S = cfg.vertsAlongEdge;
	for (uint16_t yy=0; yy<cfg.vertsAlongEdge-1; yy++)
		for (uint16_t xx=0; xx<cfg.vertsAlongEdge-1; xx++) {
			uint16_t a = ((yy+0)*S) + (xx+0);
			uint16_t b = ((yy+0)*S) + (xx+1);
			uint16_t c = ((yy+1)*S) + (xx+1);
			uint16_t d = ((yy+1)*S) + (xx+0);
			mesh.ind_buffer_cpu.push_back(a); mesh.ind_buffer_cpu.push_back(b); mesh.ind_buffer_cpu.push_back(c);
			mesh.ind_buffer_cpu.push_back(c); mesh.ind_buffer_cpu.push_back(d); mesh.ind_buffer_cpu.push_back(a);
		}

	mesh.vert_buffer_cpu.resize(cfg.vertsAlongEdge*cfg.vertsAlongEdge);
	// uint32_t y_off = (res_ratio - 1 - (ty % res_ratio)) * cfg.texSize / res_ratio;
	// uint32_t x_off = (tx % res_ratio) * cfg.texSize / res_ratio;
	uint32_t y_off = 0;
	uint32_t x_off = 0;
	uint16_t* elevData = (uint16_t*) elevBuf.data;

	for (int32_t yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (int32_t xx=0; xx<cfg.vertsAlongEdge; xx++) {
			int32_t ii = ((cfg.vertsAlongEdge-1-yy)*cfg.vertsAlongEdge)+xx;
			// int32_t ii = ((yy)*cfg.vertsAlongEdge)+xx;

			float xxx_ = static_cast<float>(xx) / static_cast<float>(cfg.vertsAlongEdge-1);
			float yyy_ = static_cast<float>(yy) / static_cast<float>(cfg.vertsAlongEdge-1);
			float xxx = (xxx_ * lvlScale) + ox;
			float yyy = ((1. - yyy_) * lvlScale) + oy;
			// float zzz = (((uint16_t*)elevBuf.buffer)[yy*cfg.vertsAlongEdge+xx]) / (8.0f * static_cast<float>(WebMercatorMapScale));
			// float zzz = (((uint16_t*)elevBuf.buffer)[(cfg.vertsAlongEdge-yy-1)*cfg.vertsAlongEdge+xx]) / (8.0f * static_cast<float>(WebMercatorMapScale));
			// float zzz = (elevData[(cfg.vertsAlongEdge-yy-1)*cfg.vertsAlongEdge+xx]) / (4.0f * static_cast<float>(WebMercatorMapScale));
			float zzz = (elevData[(yy+y_off)*elevBuf.cols+xx+x_off] / 8.0) / WebMercatorMapScale;
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

namespace {
void loadElevAndMetaNoDted(FtTile* tile, FtTypes::DecodedCpuTileData::MeshData& mesh, const FtTypes::Config& cfg) {
	int64_t tz = tile->coord.z();
	int64_t ty = tile->coord.y();
	int64_t tx = tile->coord.x();
	float lvlScale = 2. / (1 << tz);
	float ox = tx * lvlScale * 1. - 1.;
	float oy = (ty * lvlScale * 1. - 1.);

	int S = cfg.vertsAlongEdge;
	for (uint16_t yy=0; yy<cfg.vertsAlongEdge-1; yy++)
		for (uint16_t xx=0; xx<cfg.vertsAlongEdge-1; xx++) {
			uint16_t a = ((yy+0)*S) + (xx+0);
			uint16_t b = ((yy+0)*S) + (xx+1);
			uint16_t c = ((yy+1)*S) + (xx+1);
			uint16_t d = ((yy+1)*S) + (xx+0);
			mesh.ind_buffer_cpu.push_back(a); mesh.ind_buffer_cpu.push_back(b); mesh.ind_buffer_cpu.push_back(c);
			mesh.ind_buffer_cpu.push_back(c); mesh.ind_buffer_cpu.push_back(d); mesh.ind_buffer_cpu.push_back(a);
		}

	mesh.vert_buffer_cpu.resize(cfg.vertsAlongEdge*cfg.vertsAlongEdge);

	for (int32_t yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (int32_t xx=0; xx<cfg.vertsAlongEdge; xx++) {
			int32_t ii = ((cfg.vertsAlongEdge-1-yy)*cfg.vertsAlongEdge)+xx;
			// int32_t ii = ((yy)*cfg.vertsAlongEdge)+xx;

			float xxx_ = static_cast<float>(xx) / static_cast<float>(cfg.vertsAlongEdge-1);
			float yyy_ = static_cast<float>(yy) / static_cast<float>(cfg.vertsAlongEdge-1);
			float xxx = (xxx_ * lvlScale) + ox;
			float yyy = ((1. - yyy_) * lvlScale) + oy;
			mesh.vert_buffer_cpu[ii].x = xxx;
			mesh.vert_buffer_cpu[ii].y = yyy;
			mesh.vert_buffer_cpu[ii].z = 0;
			mesh.vert_buffer_cpu[ii].w = 0;
			mesh.vert_buffer_cpu[ii].u = xxx_;
			mesh.vert_buffer_cpu[ii].v = yyy_;

			// fmt::print(" - vert {} {} {} | {} {}\n", xxx,yyy,0, xxx_,yyy_);
		}

	unit_wm_to_ecef((float*)mesh.vert_buffer_cpu.data(), mesh.vert_buffer_cpu.size(), (float*)mesh.vert_buffer_cpu.data(), 6);
}
}


FtRenderer::~FtRenderer() {
}

// TODO: Note: I've moved also the 'upload' code in here since it is easier (initially meant to split it up)
int FtDataLoader::loadTile(FtTile* tile, FtTypes::DecodedCpuTileData& dtd, bool isOpen) {

	loadColor(tile, dtd.mesh);
	if (elevDset)
		loadElevAndMetaWithDted(tile, dtd.mesh, renderer.cfg);
	else
		loadElevAndMetaNoDted(tile, dtd.mesh, renderer.cfg);

	uint32_t total_meshes = 1;
	std::vector<uint32_t> gatheredIds(total_meshes);
	renderer.gtpd.withdraw(gatheredIds, !isOpen);
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
		auto &td = renderer.gtpd.datas[meshId];

		// See if we must re-allocate larger buffers.
		// If the texture needs a re-allocation, we must mark that the DescSet needs to be updated too
		// NOTE: This will not happen in ftr (but does in rt)
		auto vert_size = sizeof(FtPackedVertex)*dtd.mesh.vert_buffer_cpu.size();
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
bool FtTile::upload(FtTypes::DecodedCpuTileData& dctd, GtTileData& td) {
	// Upload vertices, indices, texture

	{
		// int meshId = tile->meshIds[0];
		// auto &td = renderer.gtpd.datas[meshId];

		// See if we must re-allocate larger buffers.
		// If the texture needs a re-allocation, we must mark that the DescSet needs to be updated too
		// NOTE: This will not happen in ftr (but does in rt)
		auto vert_size = sizeof(FtPackedVertex)*dctd.mesh.vert_buffer_cpu.size();
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

void FtTile::doRenderCasted(GtTileData& td, const CasterStuff& casterStuff) {
	// fmt::print(" - rendering casted\n");

	glBindTexture(GL_TEXTURE_2D, td.tex);
	glBindBuffer(GL_ARRAY_BUFFER, td.verts);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, td.inds);

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*6, 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*6, (void*)(4*4));

	glDrawElements(GL_TRIANGLES, td.residentInds, GL_UNSIGNED_SHORT, 0);
}

void FtTile::doRender(GtTileData& td) {

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

// Called for new tiles
void FtTile::updateGlobalBuffer(FtTypes::GlobalBuffer* gpuBuffer) {
}


void FtRenderer::initPipelinesAndDescriptors(const AppConfig& cfg) {

	// FIXME: Replace

	// TODO: 1) Compile Shaders

	normalShader = std::move(Shader{ft_tile_vsrc, ft_tile_fsrc});

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

		if (this->cfg.allowCaster) {
			do_init_caster_stuff();
			castableShader = std::move(Shader{ft_tile_casted_vsrc, ft_tile_casted_fsrc});
		}

}

}
