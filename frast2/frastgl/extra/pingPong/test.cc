#include "frast2/frastgl/core/app.h"
#include "frast2/frastgl/extra/pingPong/pingPong.hpp"

using namespace frast;

class Test_PingPong_App : public App, public PingPongHarness<Test_PingPong_App> {

	public:

	Test_PingPong_App(const AppConfig& cfg) : App(cfg), PingPongHarness(cfg.w, cfg.h, 4) {
	}



	inline virtual void doInit() override {
		pingPongSetup();
	}
	inline virtual void render(RenderState& rs) override {
	}

	inline void do_frame() {
		window.beginFrame();

		glBegin(GL_TRIANGLES);
		glColor4f(1,0,0,1);
		glVertex3f(1,0,0);
		glColor4f(0,1,0,1);
		glVertex3f(0,1,0);
		glColor4f(0,0,1,1);
		glVertex3f(0,0,1);
		glEnd();

		window.endFrame();
	}

	private:
	friend class PingPongHarness<Test_PingPong_App>;
	const std::string pingPongDownShader_vsrc = R"(#version 440
		in layout(location=0) vec2 a_pos;
		in layout(location=1) vec2 a_uv;
		out vec2 v_uv;
		void main() {
			gl_Position = vec4(a_pos, 0., 1.);
			v_uv = a_uv;
		})";
	const std::string pingPongDownShader_fsrc = R"(#version 440
		uniform layout(location=0) sampler2D u_tex;
		in vec2 v_uv;
		out vec4 o_color;
		void main() {
			vec4 c = texture(u_tex, v_uv);
			o_color = c;
		})";

	const std::string pingPongUpShader_vsrc = R"(#version 440
		in layout(location=0) vec2 a_pos;
		in layout(location=1) vec2 a_uv;
		out vec2 v_uv;
		void main() {
			gl_Position = vec4(a_pos, 0., 1.);
			v_uv = a_uv;
		})";
	const std::string pingPongUpShader_fsrc = R"(#version 440
		uniform layout(location=0) sampler2D u_tex;
		in vec2 v_uv;
		out vec4 o_color;
		void main() {
			vec4 c = texture(u_tex, v_uv);
			o_color = c;
		})";
};


int main() {

	AppConfig cfg;

	Test_PingPong_App app(cfg);
	app.init();

	while (true) {
		app.do_frame();
	}

	return 0;
}
