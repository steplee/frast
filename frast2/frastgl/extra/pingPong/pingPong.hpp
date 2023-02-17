#pragma once

#include "frast2/frastgl/core/shader.h"
#include <vector>
#include <cassert>

namespace frast {

	namespace {
		void setDefaultTexParam() {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		void checkFramebuffer() {
			auto it = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (it == GL_FRAMEBUFFER_UNDEFINED) assert(false and "framebuffer undefined");
			if (it == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT) assert(false and "framebuffer incomplete attachment");
			if (it == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) assert(false and "framebuffer missing attachment");
			if (it == GL_FRAMEBUFFER_UNSUPPORTED) assert(false and "framebuffer unsupported");
		}
	}

// Derived needs:
//     Shader pingPongDownShader
//     Shader pingPongUpShader
//
// Since we need a depth buffer only for the fbo rendered to the scene,
// we split the scene & internal fbos -- needing a total of 3 textures/fbos.


template <class Derived>
class PingPongHarness {

	public:
		inline PingPongHarness(int w, int h, int levels) : w(w), h(h), levels(levels) {
			assert(levels % 2 == 0);
		}

		inline ~PingPongHarness() {
			if (fboInternal[0] != 0) glDeleteFramebuffers(2, fboInternal);
			if (texInternal[0] != 0) glDeleteTextures(2, texInternal);
			if (fboScene != 0) glDeleteFramebuffers(1, &fboScene);
			if (texScene != 0) glDeleteTextures(1, &texScene);
			if (texSceneDepth != 0) glDeleteTextures(1, &texSceneDepth);
		}

		inline void pingPongSetup() {

			pingPongDownShader.compile(
					static_cast<Derived*>(this)->pingPongDownShader_vsrc,
					static_cast<Derived*>(this)->pingPongDownShader_fsrc);
			pingPongUpShader.compile(
					static_cast<Derived*>(this)->pingPongUpShader_vsrc,
					static_cast<Derived*>(this)->pingPongUpShader_fsrc);

			std::vector<float> empty(w*h*4, 0.f);

			glGenTextures(2, texInternal);
			for (int i=0; i<2; i++) {
				glBindTexture(GL_TEXTURE_2D, texInternal[i]);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_FLOAT, empty.data());
				// glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, w, h);
				setDefaultTexParam();
				glBindTexture(GL_TEXTURE_2D, 0);
			}

			glGenFramebuffers(2, fboInternal);
			for (int i=0; i<2; i++) {
				glBindFramebuffer(GL_FRAMEBUFFER, fboInternal[i]);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texInternal[i], 0);
				checkFramebuffer();
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}

			glGenTextures(1, &texScene);
			glBindTexture(GL_TEXTURE_2D, texScene);
			// glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA, w, h);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_FLOAT, empty.data());
			setDefaultTexParam();

			glGenTextures(1, &texSceneDepth);
			glBindTexture(GL_TEXTURE_2D, texSceneDepth);
			// glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, w, h);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, empty.data());
			glBindTexture(GL_TEXTURE_2D, 0);

			glGenFramebuffers(1, &fboScene);
			glBindFramebuffer(GL_FRAMEBUFFER, fboScene);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texScene, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texSceneDepth, 0);
			checkFramebuffer();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		inline void pingPongRender(GLuint fboFinal=0) {

			glDisable(GL_DEPTH_TEST);

			int I = 1;

			// ---------------------------------------------------
			// Down
			//
			for (int i=0; i<levels; i++) {
				// auto fboSrc = fboInternal[I];
				auto fboTarget = fboInternal[1-I];
				auto texSrc = texInternal[I];
				// auto texTarget = texInternal[1-I];
				if (i == 0) texSrc = texScene;

				
				I = (I+1) & 3;
			}

			// ---------------------------------------------------
			// Up
			//

			for (int i=levels-1; i>=0; i--) {
				auto fboSrc = fboInternal[I];
				auto fboTarget = fboInternal[1-I];
				auto texSrc = texInternal[I];
				// auto texTarget = texInternal[1-I];
				if (i == 0) fboTarget = fboFinal;

				I = (I+1) & 3;
			}

			glEnable(GL_DEPTH_TEST);

		}

		GLuint getSceneFbo() const { return fboInternal[0]; }

	private:
		int w, h, levels;
		Shader pingPongDownShader, pingPongUpShader;

		// private GLuint fboScene;
		GLuint fboInternal[2] = {0};
		GLuint texInternal[2] = {0};

		GLuint fboScene=0, texScene=0, texSceneDepth;

		inline void renderFullScreenQuad(GLuint program) {
			static const float verts[] = {
				-1, -1,    0, 0,
				 1, -1,    1, 0,
				 1,  1,    1, 1,

				 1,  1,    1, 1,
				-1,  1,    0, 1,
				-1, -1,    0, 0 };

			glUseProgram(program);
			glEnableVertexAttribArray(0); // pos
			glEnableVertexAttribArray(1); //  uv
			glVertexAttribPointer(0, 2, GL_FLOAT, false, 4*4,   verts);
			glVertexAttribPointer(1, 2, GL_FLOAT, false, 4*4, 2+verts);
			glDisableVertexAttribArray(0);
			glDisableVertexAttribArray(1);
		}
	
};

}
