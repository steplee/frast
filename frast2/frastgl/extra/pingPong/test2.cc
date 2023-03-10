#include "frast2/frastgl/core/app.h"
#include "frast2/frastgl/extra/pingPong/pingPong2.hpp"

using namespace frast;

class Test_PingPong_App : public App, public PingPongHarness2<Test_PingPong_App> {

	public:

	Test_PingPong_App(const AppConfig& cfg) : App(cfg), PingPongHarness2(cfg.w, cfg.h, 4) {
	}



	inline virtual void doInit() override {
		pingPongSetup();
	}
	inline virtual void render(RenderState& rs) override {
	}

	float t = 0.f;
	int frame = 0;

	inline void do_frame() {
		frame++;

		bool use_effect = frame % 120 >= 30;

		window.beginFrame();

		glBlendFunc(GL_ONE, GL_ZERO);
		glEnable(GL_DEPTH_TEST);

		if (use_effect)
			glBindFramebuffer(GL_FRAMEBUFFER, pingPongGetSceneFbo());
		else
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		t += .07f;
		glRotatef(t,0,0,1.f);

		glDisable(GL_BLEND);
		glBegin(GL_TRIANGLES);
		glColor4f(1,0,0,1);
		glVertex3f(1,-1,0);
		glColor4f(0,1,0,1);
		glVertex3f(-1,1,0);
		glColor4f(0,0,1,1);
		glVertex3f(-1,-1,0);
		glEnd();

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glRotatef(-t,.6f,.1f,1.f);
		glBegin(GL_TRIANGLES);
		glColor4f(0,0,0,0);
		glVertex3f(.5,-.5,-.001f);
		glVertex3f(-.2,.1,-.001f);
		glVertex3f(-1,-1,-.001f);
		glEnd();

		glEnable(GL_BLEND);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glRotatef(-t,1.2f,.1f,.3f);
		glBegin(GL_TRIANGLES);
		glColor4f(0,0,1,.1f);
		glVertex3f(-.5,-.1,-.001f);
		glVertex3f( .2,-.1,-.001f);
		glVertex3f( .5f, 2,-.001f);
		glEnd();


		if (use_effect)
			pingPongRender(0);

		window.endFrame();
	}

	private:
	friend class PingPongHarness2<Test_PingPong_App>;

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
		uniform layout(location=1) ivec2 u_wh;
		in vec2 v_uv;
		out vec4 o_color;
		void main() {

			vec2 uv = v_uv;
			vec4 c;

			// One Sample Version
			if (false) {
				c = texture(u_tex, uv);
			} else {

				// Quad Sample Version
				float B = .25;
				vec4 d = vec4(-B/vec2(u_wh), B/vec2(u_wh));
				c = .25 * (texture(u_tex, uv+d.xy)
						+ texture(u_tex, uv+d.zy)
						+ texture(u_tex, uv+d.zw)
						+ texture(u_tex, uv+d.xw));
			}

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
		uniform layout(location=1) ivec2 u_wh;
		in vec2 v_uv;
		out vec4 o_color;
		void main() {

			vec2 uv = v_uv;
			vec4 c;

			// One Sample Version
			// c = texture(u_tex, uv);

			// Quad Sample Version
			float B = .25*.5;
			vec4 d = vec4(-B/vec2(u_wh), B/vec2(u_wh));
			c = .25 * (texture(u_tex, uv+d.xy)
			         + texture(u_tex, uv+d.zy)
			         + texture(u_tex, uv+d.zw)
			         + texture(u_tex, uv+d.xw));

			o_color = c;
		})";

	// These can be empty, then the impl will render the last 'up' directly to \fboFinal
	/*
	const std::string pingPongFinalMerge_vsrc = R"(#version 440
		in layout(location=0) vec2 a_pos;
		in layout(location=1) vec2 a_uv;
		out vec2 v_uv;
		void main() {
			gl_Position = vec4(a_pos, 0., 1.);
			v_uv = a_uv;
		})";
	const std::string pingPongFinalMerge_fsrc = R"(#version 440
		uniform layout(location=0) sampler2D u_tex_scene;
		uniform layout(location=1) sampler2D u_tex_filtered;
		uniform layout(location=2) ivec2 u_wh;
		in vec2 v_uv;
		out vec4 o_color;
		void main() {

			vec2 uv = v_uv;

			vec4 s = texture(u_tex_scene, uv);
			vec4 f = texture(u_tex_filtered, uv);

			vec4 c = s.a * vec4(s.rgb,1.) + (1. - s.a) * f.rgba;

			o_color = c;
		})";
		*/
	const std::string pingPongFinalMerge_vsrc = "";
	const std::string pingPongFinalMerge_fsrc = "";

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

