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
		inline void helper_handleCompletedHeadlessRender(RenderState& rs, FrameData& fd) {
			Derived* self = (Derived*) this;

			// We can re-use the command+fence, because render() waits on the frameDoneFence so cpu is in sync.
			Command& cmd = fd.cmd;
			Fence& fence = fd.frameDoneFence;

			cmd.copy

			Submission submission { DeviceQueueSpec{self->mainDevice,self->mainQueue} };
			submission.fence = fence;
			// submission.signalSemas = { headlessCopyDoneSema };
			// submission.waitSemas = { fd.renderCompleteSema };
			// submission.waitStages = { VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT };
			// submission.submit(&fd.cmd.cmdBuf, 1, false); // Do NOT block on fence, do NOT reset fence -- that way frameAvailableFence won't be set until we read it in acquireNextFrame()!!!
			submission.submit(&cmd.cmdBuf, 1);
			fmt::print(" - handled done\n");
		}

	protected:

		ExBuffer bufColor, bufDepth;
};
