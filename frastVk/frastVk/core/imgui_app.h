#pragma once

#include "app.h"



class ImguiApp : BaseVkApp {
	public:
		ImguiApp();
		virutal ~ImguiApp();

		virtual void render();
		virtual void doRender(RenderState& rs) =0;
		inline virtual void postRender() {}
		inline virtual void handleCompletedHeadlessRender(RenderState& rs) {};

		inline virtual uint32_t mainSubpass() const override { return 1; }

		bool isDone();

		virtual void handleKey(uint8_t key, uint8_t mod, bool isDown) override;
		
	protected:

		bool isDone_ = false;

		std::shared_ptr<Camera> camera = nullptr;
		RenderState renderState;
};
