#pragma once


// -----------------------------------------------------------
// Gt Impl
// -----------------------------------------------------------

////////////////////////////
// GtTile
////////////////////////////

template <class GtTypes, class Derived>
void GtTile<GtTypes,Derived>::unload(typename GtTypes::UpdateContext& gtuc) {
	loaded = false;
	if (meshIds.size()) {
		gtuc.pooledData.deposit(meshIds);
		meshIds.clear();
	}
}

template <class GtTypes, class Derived>
void GtTile<GtTypes,Derived>::render(typename GtTypes::RenderContext& gtrc) {
	if (inner()) {
		for (auto c : children) c->render(gtrc);
	}

	else if (leaf()) {
		assert(loaded);

		for (int mi=0; mi<meshIds.size(); mi++) {
			auto idx = meshIds[mi];
			auto &td = gtrc.pooledData.datas[idx];
			if (td.residentInds <= 0) continue;

			// TODO Uncomment these
			//gtrc.cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*td.verts.buffer}, {0u});
			//gtrc.cmd.bindIndexBuffer(*td.inds.buffer, 0u, vk::IndexType::eUint16);

			if (sizeof(typename GtTypes::PushConstants) > 0) {
				typename GtTypes::PushConstants pushc{(Derived*)this, idx};
				//gtrc.cmd.pushConstants(*gtrc.pipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const typename GtTypes::PushConstants>{1, &pushc});
				vkCmdPushConstants(gtrc.cmd, gtrc.gfxPipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(typename GtTypes::PushConstants), &pushc);
			}

			//gtrc.cmd.drawIndexed(td.residentInds, 1, 0,0,0);
		}
		gtrc.drawCount += meshIds.size();
	}
}

template <class GtTypes, class Derived>
typename GtTile<GtTypes,Derived>::UpdateStatus GtTile<GtTypes,Derived>::update(typename GtTypes::UpdateContext& gtuc, bool sseIsCached) {

	// if (openingAsLeaf() or closing()) {
	if (opening() or openingAsLeaf() or closing()) {
		//fmt::print(" - update {} [SKIPPING]\n", toString());
		return UpdateStatus::eNone;
	}

	if (leaf() or invalid()) {
		if (not sseIsCached) lastSSE = bb.computeSse(gtuc)*geoError;
		//if (coord.len < 9) lastSSE = 3;
		//fmt::print(" - update {}\n", toString());
		// fmt::print(" - {} has sse {}\n", coord.toString(), lastSSE);

		if (!root() and lastSSE < gtuc.sseThresholdClose) {
			// Should not close while opening a subtree
			assert (gtuc.currentOpenAsk.ancestor == nullptr);
			return UpdateStatus::eClose;

		} else if (lastSSE > gtuc.sseThresholdOpen) {
			if (terminal()) return UpdateStatus::eNone;

			bool loadMe = true;

			// Begin collecting subtree
			if (gtuc.currentOpenAsk.ancestor == nullptr) {
				assert(gtuc.currentOpenAsk.tiles.size() == 0);
				gtuc.currentOpenAsk.ancestor = (Derived*)this;
			}

			//if (!terminal())
			{
				// Create all children, if any want to open,
				// transition self to inner and recursively call update() on all children.
				if (children.size() == 0) ((Derived*)this)->createChildren(gtuc);
				if (children.size() == 0) flags |= TERMINAL;
				if (terminal()) {
					if (gtuc.currentOpenAsk.ancestor == (Derived*)this)
						gtuc.currentOpenAsk.ancestor = nullptr;
					return UpdateStatus::eNone;
				}

				assert(children.size() > 0); // or else would be terminal

				bool anyChildrenWantOpen = false;
				for (auto &child : children) {
					child->lastSSE = child->bb.computeSse(gtuc)*geoError;
					//if (child->coord.len < 9) child->lastSSE = 3;
					if (child->lastSSE > gtuc.sseThresholdOpen) anyChildrenWantOpen = true;
					// fmt::print(" - {} has sse {} (child)\n", coord.toString(), lastSSE);
				}

#warning "cascading open disabled, there is a bug: TODO"
				if (false and anyChildrenWantOpen) {
				// if (anyChildrenWantOpen) {
					fmt::print(" - (t {}) some children want to open, shortcutting.\n", coord.toString());
					loadMe = false;
					// state = INNER;
					flags |= OPENING;
					for (auto &child : children) {
						// child->state = LEAF;
						// lastSSE was just compute and set on the child
						child->update(gtuc, true);
					}
				}
			}

			GtAsk<GtTypes> &ask = gtuc.currentOpenAsk;
			if (loadMe) {
				//fmt::print(" - (t {}) opening {} children (ask ancestor {}).\n", coord.toString(), children.size(), gtuc.currentOpenAsk.ancestor->coord.toString());
				//state = INNER;
				flags |= OPENING;
				for (auto &c : children) {
					c->flags |= OPENING_AS_LEAF;
					ask.tiles.push_back(c);
				}
			} else {
				// Even if some children are recursively loading and we dont 'loadMe', we still must sweep and add terminal children!
				for (auto &c : children) {
					if (c->terminal()) {
						c->flags |= OPENING_AS_LEAF;
						ask.tiles.push_back(c);
					}
				}
			}
			if (gtuc.currentOpenAsk.ancestor == (Derived*)this) {
				ask.isOpen = true;
				gtuc.dataLoader.pushAsk(ask);
				gtuc.currentOpenAsk.ancestor = nullptr;
			}


			// TODO queue open
			return UpdateStatus::eOpen;
		} else {
			return UpdateStatus::eNone;
		}

	} else {
		// inner
		// fmt::print(" - update {}\n", toString());
		bool allClose = true, allOpen = true;
		std::vector<UpdateStatus> stats(children.size());
		for (int i=0; i<children.size(); i++) {
			auto& c = children[i];
			auto stat = c->update(gtuc);
			if (stat != UpdateStatus::eOpen) allOpen = false;
			if (stat != UpdateStatus::eClose) allClose = false;
			stats[i] = stat;
		}

		// TODO: first I will only support closing one level at a time. The full-recursive case is trickier
		if (allClose) {

			if (not sseIsCached) lastSSE = bb.computeSse(gtuc)*geoError;
			//if (coord.len < 9) lastSSE = 3;
			if (lastSSE > gtuc.sseThresholdOpen) {
				fmt::print(" - (t {}) children want to close, but this tile would just re-open. Not doing anything.)\n", coord.toString());
				return UpdateStatus::eNone;
			}

			// Close child leaves, with this node becoming a leaf.
			GtAsk<GtTypes> newAsk;
			newAsk.isOpen = false;
			newAsk.ancestor = (Derived*)this;
			flags |= CLOSING_CHILDREN;

			for (int i=0; i<children.size(); i++) {
				assert(children[i]->leaf());
				assert(not children[i]->opening());
				assert(not children[i]->closing());
				children[i]->flags |= CLOSING;
			}
			newAsk.tiles.push_back((Derived*)this);
			fmt::print(" - Asking to close {} ({} children)\n", coord.toString(), children.size());
			gtuc.dataLoader.pushAsk(newAsk);

			// Must return none!
			return UpdateStatus::eNone;
		} else {
			// not all child leafs don't want to close, nothing to do!
			return UpdateStatus::eNone;
		}

		/*
		if (allClose) {
			if (not sseIsCached) lastSSE = bb.computeSse(gtuc);


			if (!root() and lastSSE < gtuc.sseThresholdClose) {
				for (int i=0; i<children.size(); i++) children[i]->flags |= CLOSING;
				return UpdateStatus::eClose;
			} else {
				// Close them, with this tile becoming a leaf.

				GtAsk<GtTypes> newAsk;
				newAsk.isOpen = false;
				newAsk.ancestor = (Derived*)this;
				flags |= CLOSING_CHILDREN;

				for (int i=0; i<children.size(); i++) {
						assert(not children[i]->opening());
						assert(not children[i]->closing());
						children[i]->flags |= CLOSING;
						newAsk.tiles.push_back(children[i]);
				}
				gtuc.dataLoader.pushAsk(newAsk);
			}

		} else {

			// Close only those that need closing
			for (int i=0; i<children.size(); i++) {
				if (stats[i] == UpdateStatus::eClose) {
					assert(not children[i]->opening());
					assert(not children[i]->closing());
					GtAsk<GtTypes> newAsk0;
					children[i]->flags |= CLOSING;
					newAsk0.ancestor = (Derived*)this;
					newAsk0.isOpen = false;
					newAsk0.tiles.push_back(children[i]);
					gtuc.dataLoader.pushAsk(newAsk0);
				}
			}
			flags |= CLOSING_CHILDREN;

		}
		*/
	}
	return UpdateStatus::eNone;
}

////////////////////////////
// GtRenderer
////////////////////////////

template <class GtTypes, class Derived>
void GtRenderer<GtTypes,Derived>::pushResult(GtAsk<GtTypes>& ask) {
	std::lock_guard<std::mutex> lck(askMtx);
	waitingResults.push_back(ask);
}

template <class GtTypes, class Derived>
void GtRenderer<GtTypes,Derived>::update(GtUpdateContext<GtTypes>& gtuc) {
	// 1) Traverse tree, compute sse, update state, make any queries as needed
	// 2) Handle loaded results
	//
	// TODO:
	// Decide whether to have third thread that just computes sse : NO
	// Decide whether to have a map of OBBs seperate from tile data in files : YES
	//

	// 1)

	for (auto& root : roots) {
		// fmt::print(" (update root {})\n", root->coord.toString());
		root->update(gtuc);
	}

	// Debug entire tree (prints a lot)
	if (0) {
		fmt::print(" - PostUpdate\n");
		std::stack<typename GtTypes::Tile*> st;
		for (auto rt : roots) st.push(rt);
		while (not st.empty()) {
			auto t = st.top();
			st.pop();
			for (auto c : t->children) st.push(c);
			fmt::print(" - {}\n", t->toString());
		}
	}

	// 2)

	auto writeImgDesc = [&](GtTileData& td, uint32_t idx) {
		VkDescriptorImageInfo imgInfo { sampler, gtpd.datas[idx].tex.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
		gtpd.tileDescSet.update(VkWriteDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
				gtpd.globalDescSet, 0,
				0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				&imgInfo, 0, nullptr
				});
		// td.texOld.; // Free
	};
	auto checkWriteImgDesc = [&](RtTile* tile) {
		for (auto& idx : tile->meshIds) {
			if (gtpd.datas[idx].mustWriteImageDesc) {
				gtpd.datas[idx].mustWriteImageDesc = false;
				writeImgDesc(gtpd.datas[idx], idx);
			}
		}
	};

	decltype(waitingResults) theResults;
	{
		std::lock_guard<std::mutex> lck(askMtx);
		theResults = std::move(waitingResults);
		waitingResults.clear();
	}

	if (theResults.size()) {
		int ntot = 0;
		for (auto &r : theResults) ntot += r.tiles.size();
		fmt::print(fmt::fg(fmt::color::yellow), " - [#update] handling {} loaded results ({} total tiles)\n", theResults.size(), ntot);

		for (auto &r : theResults) {
			/*
			auto parent = r.ancestor;

			if (r.isOpen) {
				if (parent) assert(parent->opening());
				if (parent) assert(not parent->closing());
				for (auto tile : r.tiles) {
					assert(tile->openingAsLeaf());
					assert(not tile->opening());
					assert(not tile->closing());
					tile->flags &= ~GtTypes::Tile::OPENING_AS_LEAF;
					tile->loaded = true;
				}
				if (parent) {
					assert(parent->flags & GtTypes::Tile::OPENING);
					parent->flags &= ~GtTypes::Tile::OPENING;
					parent->unload(gtuc);
				}
			} else {

				assert(parent);
				assert(parent->closingChildren());
				assert(not parent->loaded);
				assert(parent->leaf());
				for (auto c : parent->children) {
					assert(c->closing());
					c->unload(gtuc);
					delete c;
				}
				parent->loaded = true;
				parent->state = GtTypes::Tile::LEAF;
			}
		*/

			if (r.isOpen) {
				for (auto tile : r.tiles) {
					fmt::print(" - handle {} (anc {})\n", tile->coord.toString(), r.ancestor ? r.ancestor->coord.toString() : "null");
					//if (r.ancestor) assert(r.ancestor->leaf() or r.ancestor->invalid());
					// assert(tile->invalid());
					assert(tile->openingAsLeaf());
					assert(not tile->opening());
					assert(not tile->closing());
					tile->state = GtTypes::Tile::LEAF;
					tile->flags &= ~GtTypes::Tile::OPENING_AS_LEAF;
					tile->loaded = true;
					
					for (auto up = tile->parent; up; up = up->parent) {
						//fmt::print(" - Setting no longer opening: {}\n", up->coord.toString());
						if (up->opening()) up->flags &= ~GtTypes::Tile::OPENING;
						if (!up->inner()) up->state = GtTypes::Tile::INNER;
						if (up->loaded) up->unload(gtuc);
						if (up == r.ancestor) break;
					}

					tile->updateGlobalBuffer((typename GtTypes::GlobalBuffer*)gtpd.globalBuffer.mappedAddr);
				}
			}

			else {
				// Supports only one-level closing (see above)
				assert(r.ancestor);
				assert(r.ancestor->closingChildren());
				assert(r.ancestor->inner());
				assert(r.tiles.size() == 1);
				auto tile = r.tiles[0];
				assert(tile->children.size());
				for (auto child : tile->children) {
					assert(child->closing());
					assert(child->leaf());
					child->unload(gtuc);
					delete child;
				}
				tile->children.clear();
				tile->state = GtTypes::Tile::LEAF;
				tile->flags &= ~GtTypes::Tile::CLOSING_CHILDREN;
				tile->loaded = true;

				tile->updateGlobalBuffer((typename GtTypes::GlobalBuffer*)gtpd.globalBuffer.mappedAddr);
			}

			//void RtTile::updateGlobalBuffer(RtTypes::GlobalBuffer& gpuBuffer) {
		}
	}

}

template <class GtTypes, class Derived>
void GtRenderer<GtTypes,Derived>::render(RenderState& rs, Command& cmd) {
	// Have a pointer indirection here for if we want to use the caster pipeline
	GraphicsPipeline* thePipeline = nullptr;
	//cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
	//cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 1, {1,&*gtpd.descSet}, nullptr);
	//cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
	thePipeline = &gfxPipeline;

	typename GtTypes::RenderContext gtrc { rs, gtpd, *thePipeline, cmd };

	/*simpleRenderPass.begin(fd.cmd, fd);
	vkCmdBindPipeline(fd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipeline);
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(fd.cmd, 0, 1, &vertexBuffer.buf, &offset);
	vkCmdBindIndexBuffer(fd.cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
	vkCmdBindDescriptorSets(fd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipeline.layout, 0, 1, &dset.dset, 0, 0);
	vkCmdDrawIndexed(fd.cmd, numInds, 1, 0, 0, 0);
	simpleRenderPass.end(fd.cmd, fd);*/

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *thePipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, thePipeline->layout, 0, 1, &gtpd.globalDescSet.dset, 0, 0);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, thePipeline->layout, 1, 1, &gtpd.tileDescSet.dset, 0, 0);

	float mvp[16];

	for (auto root : roots) {
		root->render(gtrc);
	}

}


template <class GtTypes, class Derived>
void GtRenderer<GtTypes,Derived>::init(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, const AppConfig& cfg) {
	device = &d;

	sampler.create(d, VkSamplerCreateInfo{

			VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			nullptr, 0,

			// VkFilter                magFilter; VkFilter                minFilter;
			// VkSamplerMipmapMode     mipmapMode;
			// VkSamplerAddressMode    addressModeU; VkSamplerAddressMode    addressModeV; VkSamplerAddressMode    addressModeW;
			VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			// VK_SAMPLER_MIPMAP_MODE_LINEAR,
			VK_SAMPLER_MIPMAP_MODE_NEAREST,
			//VK_SAMPLER_ADDRESS_MODE_REPEAT
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,

			// float                   mipLodBias;
			// VkBool32                anisotropyEnable; float                   maxAnisotropy;
			// VkBool32                compareEnable; VkCompareOp             compareOp;
			// float                   minLod; float                   maxLod;
			// VkBorderColor           borderColor;
			// VkBool32                unnormalizedCoordinates;
			0.f,
			VK_FALSE, 0.f,
			VK_FALSE, VK_COMPARE_OP_LESS,
			0, 0,
			VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			GtTypes::unnormalizedTextureCoords
	});

	gtpd.globalBuffer.set(sizeof(typename GtTypes::GlobalBuffer),
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	gtpd.globalBuffer.create(d);
	gtpd.globalBuffer.map();

	static_cast<Derived*>(this)->initPipelinesAndDescriptors(dpool, pass, cfg);

	loader.init(d, 1);
}

////////////////////////////
// GtPooledData
////////////////////////////

template <class GtTypes>
GtPooledData<GtTypes>::GtPooledData() {
	available.resize(GT_NUM_TILES);
	for (int i=0; i<GT_NUM_TILES; i++) available[i] = i;
	n_available_ = GT_NUM_TILES;
}
template <class GtTypes>
bool GtPooledData<GtTypes>::withdraw(std::vector<uint32_t>& ids, bool isClose) {
	std::lock_guard<std::mutex> lck(availableMtx);

	assert(available.size() >= ids.size());

	// TODO close/open logic: fail if not close and too few.
	for (int i=0; i<ids.size(); i++) {
		ids[i] = available.back();
		available.pop_back();
	}

	return false;
}
template <class GtTypes>
bool GtPooledData<GtTypes>::deposit(const std::vector<uint32_t>& ids) {
	std::lock_guard<std::mutex> lck(availableMtx);

	for (int i=0; i<ids.size(); i++) {
		assert(ids[i] < GT_NUM_TILES);
		available.push_back(ids[i]);
		assert(available.size() <= GT_NUM_TILES);
	}

	return false;
}

////////////////////////////
// GtDataLoader
////////////////////////////


template <class GtTypes, class Derived>
void GtDataLoader<GtTypes,Derived>::init(Device& d, uint32_t newQueueNumber) {

	// Create uploader
	uploaderQueue = std::move(Queue{d,d.queueFamilyGfxIdxs[0],(int)newQueueNumber});
	uploader.create(&d, &uploaderQueue);

	thread = std::thread(&GtDataLoader::internalLoop, this);
}

template <class GtTypes, class Derived>
void GtDataLoader<GtTypes,Derived>::pushAsk(GtAsk<GtTypes>& ask) {
	mtx.lock();
	asks.push_back(std::move(ask));
	mtx.unlock();
	cv.notify_one();
	ask.ancestor = nullptr;
}

template <class GtTypes, class Derived>
void GtDataLoader<GtTypes,Derived>::internalLoop() {
	fmt::print(" - [GtDataLoader] starting loader thread\n");

	while (true) {
		std::unique_lock<std::mutex> lck(mtx);
		cv.wait(lck, [this]() { return doStop or asks.size(); });

		if (doStop) break;

		// Move the @asks member to a temporary buffer, and empty it.
		decltype(asks) curAsks;
		{
			curAsks = std::move(asks);
			asks.clear();
			lck.unlock();
		}

		fmt::print(fmt::fg(fmt::color::olive), " - [#loader] handling {} asks\n", curAsks.size());
		for (auto ask : curAsks)
			if (ask.ancestor)
				fmt::print(fmt::fg(fmt::color::olive), " - [#loader] ancestor {}\n", ask.ancestor->coord.toString());
			else
				fmt::print(fmt::fg(fmt::color::olive), " - [#loader] ancestor <null>\n");

		while (curAsks.size()) {
			auto &ask = curAsks.back();

			// We do this in two phases, because we don't know the number of meshes apriori
			// So
			//    1) Load all data from disk, in whatever specific format (But @DecodedCpuTileData must have a '@meshes' vector member)
			//    2) Withdraw tiles from @available
			//    3) Upload the decoded data

			// XXX TODO ACTUALLY: Do them one-by-one.
			std::vector<typename GtTypes::DecodedCpuTileData> decodedTileDatas(ask.tiles.size());

			int total_meshes = 0;
			for (int i=0; i<ask.tiles.size(); i++) {
				auto &tile = ask.tiles[i];
				int n_meshes = static_cast<Derived*>(this)->loadTile(tile, decodedTileDatas[i]);
				// int n_meshes = 0;
				total_meshes += n_meshes;
			}

			/*
			std::vector<uint32_t> gatheredIds(total_meshes);
			renderer.gtpd.withdraw(gatheredIds, !ask.isOpen);


			int mesh_ii = 0;
			for (int i=0; i<ask.tiles.size(); i++) {

				auto &tile = ask.tiles[i];
				auto &coord = tile->coord;

				for (auto &meshData : decodedTileDatas[i].meshes) {
					uint32_t idx = gatheredIds[mesh_ii++];
					tile->meshIds.push_back(idx);
					// auto error = static_cast<Derived*>(this)->uploadTile(tile, meshData, renderer.gtpd.datas[idx]);
					auto error = tile->upload(meshData, renderer.gtpd.datas[idx]);
					assert(not error);
				}

				fmt::print(fmt::fg(fmt::color::dark_gray), " - [#loader] loaded {}\n", tile->coord.toString());
				tile->loaded = true;
			}
			*/


			renderer.pushResult(ask);

			curAsks.pop_back();
		}

		// TODO: If any asks failed, copy them back to the @asks member

	}
};

////////////////////////////
// GtBoundingBox
////////////////////////////

template <class GtTypes>
float GtBoundingBox<GtTypes>::computeSse(const typename GtTypes::UpdateContext& gtuc) const {

	/*
	 gtuc:
			RowMatrix4f mvp;
			Vector3f zplus;
			Vector3f eye;
			Vector2f wh;
			float two_tan_half_fov_y;
	*/

	Vector3f ctr_ = (gtuc.mvp * ctr.homogeneous()).hnormalized();
	// fmt::print(" - proj ctr {}\n", ctr_.transpose());
	Eigen::AlignedBox<float,3> cube { Vector3f{-1,-1,0}, Vector3f{1,1,1} };
	if (!cube.contains(ctr_)) {
		Eigen::Matrix<float,8,3,Eigen::RowMajor> corners;
		for (int i=0; i<8; i++)
			corners.row(i) << i%2, (i/2)%2, (i/4);
		RowMatrix3f R = q.toRotationMatrix();
		// corners = ((corners * 2 - 1).rowwise() * R.transpose() * extents).rowwise() + ctr;
		corners = ((corners.array() * 2 - 1).matrix() * R.transpose()).rowwise() + ctr.transpose();
		bool anyInside = false;
		for (int i=0; i<8; i++) if (cube.contains(corners.row(i).transpose())) { anyInside = true; break; }

		// Neither the center is in the box, nor any corner. Therefore the SSE is zero.
		if (!anyInside) return 0;
	}

	// Either center or any corner in box. Now evalauted oriented-box exterior distance to eye.
	// To get that distance I use a the function "sdBox" from IQ's raymarching function collection.
	// https://iquilezles.org/articles/distfunctions/

	RowMatrix3f R = q.toRotationMatrix();
	Vector3f q = R.transpose() * (gtuc.eye - ctr);
	float exteriorDistance = q.array().max(0.f).matrix().norm() + std::min(q.maxCoeff(), 0.f);

	float sse_without_geoErr = gtuc.wh(1) / (exteriorDistance * gtuc.two_tan_half_fov_y);
	// fmt::print(" - (sseWoGeoErr {}) (extDist {})\n", sse_without_geoErr, exteriorDistance);
	return sse_without_geoErr; // Note: Still multiply by geoError of tile
}





/*
int main() {

	RtRenderer renderer;

	sleep(1);


	for (int i=0; i<100; i++) {
		fmt::print(" (update)\n");
		GtUpdateContext<RtTypes> gtuc { renderer.loader, *renderer.obbMap, renderer.gtpd };
		gtuc.sseThresholdClose = .9;
		gtuc.sseThresholdOpen = 1.5;
		gtuc.two_tan_half_fov_y = 1.;
		gtuc.wh.setConstant(512);
		gtuc.mvp.setIdentity();
		gtuc.zplus = decltype(gtuc.zplus)::UnitZ();
		renderer.update(gtuc);
		sleep(1);


		fmt::print(" (render)\n");
		RenderState rs;
		Command cmd;
		// vk::raii::CommandBuffer cmd_ {nullptr};
		// auto cmd = *cmd_;
		renderer.render(rs, cmd);
	}


	return 0;
}
*/

