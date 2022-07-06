#pragma once

// This works, but the downstream app would need yet another virtual method to received the copied-to-cpu results,
// so it doesn't actually buy you much
/*
template <class Derivee>
struct HeadlessCopyMixin : public Derivee {
	public:
		using Derivee::Derivee;
*/

template <class Derived>
struct HeadlessCopyMixin {
		inline HeadlessCopyMixin() {
		}

		inline void initHeadlessCopyMixin() {
			Derived* self = (Derived*) this;

			bufColor.set(self->windowWidth*self->windowHeight*4,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_BUFFER_USAGE_TRANSFER_DST_BIT);
			bufColor.create(self->mainDevice);
			bufDepth.set(self->windowWidth*self->windowHeight*4,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_BUFFER_USAGE_TRANSFER_DST_BIT);
			bufDepth.create(self->mainDevice);

			// Keep mapped.
			bufColor.map();
			bufDepth.map();
		}

		// After calling this, bufColor/Depth will be populated with new frames output, mapped to cpu
		inline void helper_handleCompletedHeadlessRender(RenderState& rs, FrameData& fd, ExImage* depthImagePtr) {
			Derived* self = (Derived*) this;

			// We can re-use the command+fence, because render() waits on the frameDoneFence so cpu is in sync.
			Command& cmd = fd.cmd;

			// cmd.copy

			cmd.begin();
			if (wantColor) cmd.copyImageToBuffer(bufColor, *fd.swapchainImg);
			if (wantDepth and depthImagePtr != nullptr) cmd.copyImageToBuffer(bufDepth, *depthImagePtr, VK_IMAGE_LAYOUT_UNDEFINED);
			cmd.end();

			if (wantColor) {
				lastRenderState = rs;
				lastRenderState.frameData = nullptr;
			}

			Submission submission { DeviceQueueSpec{self->mainDevice,self->mainQueue} };
			submission.fence = fd.frameAvailableFence;
			submission.waitSemas = { fd.renderCompleteSema };
			submission.waitStages = { VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT };
			submission.submit(&cmd.cmdBuf, 1);

			// std::ofstream ofs("frame.bin", std::ios::out | std::ios::binary);
			// ofs.write((const char*)bufColor.mappedAddr, fd.swapchainImg->extent.width*fd.swapchainImg->extent.height*4);
		}

	protected:

		bool wantColor:1 = true;
		bool wantDepth:1 = true;

		ExBuffer bufColor, bufDepth;
		RenderState lastRenderState;
};
