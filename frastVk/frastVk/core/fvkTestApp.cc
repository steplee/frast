#include "fvkApi.h"
#include "fvkShaders.h"
#include "imgui_app.h"
#include <fmt/color.h>

// using BaseClass = BaseApp;
using BaseClass = ImguiApp;

struct TestApp : public BaseClass {
	int i = 0;

	using BaseClass::BaseClass;

	GraphicsPipeline triPipeline;

	inline virtual void initVk() {
		BaseClass::initVk();

		CameraSpec spec { 512,512, 45. };
		camera = std::make_shared<SphericalEarthMovingCamera>(spec);

		assert(not loadShader(mainDevice,
				triPipeline.vs,
				triPipeline.fs,
				"simple/tri"));

		float viewportXYWH[4] = {
			0,0,
			(float)windowWidth,
			(float)windowHeight
		};
		PipelineBuilder builder;
		builder.depthTest = cfg.depth;

		VertexInputDescription vid;
		/*
		VkVertexInputAttributeDescription positionAttribute;
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		positionAttribute.offset = 0;
		vid.attributes.push_back(positionAttribute);
		VkVertexInputBindingDescription binding_ = { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX };
		vid.bindings.push_back(binding_);
		*/

		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, triPipeline.vs, triPipeline.fs);

		triPipeline.create(mainDevice, viewportXYWH, builder, simpleRenderPass, 0);
		fmt::print(fmt::fg(fmt::color::green), " - Created simple/tri pipleline!\n");
	}

	inline virtual void doRender(RenderState& rs) override {

		auto& fd = *rs.frameData;

		fd.cmd.begin();

		fd.cmd.clearImage(*fd.swapchainImg, {(float)(i%3==0), (float)(i%3==1), (float)(i%3==2), 1.f});

		simpleRenderPass.begin(fd.cmd, fd);
		vkCmdBindPipeline(fd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipeline);
		vkCmdDraw(fd.cmd, 3, 1, 0, 0);
		simpleRenderPass.end(fd.cmd, fd);

		i++;

	}
};

int main() {
	AppConfig cfg;

	TestApp app(cfg);
	app.initVk();

	for (int i=0; i<20; i++) {
		app.render();
		usleep(200'000);
	}

	return 0;
}
