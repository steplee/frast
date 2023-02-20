#pragma once

#include "frast2/frastgl/core/shader.h"
#include <vector>
#include <cassert>
#include <stdexcept>
#include <fmt/core.h>


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
//
// WARNING:
// The problem with this class is that it does not store textures
// past the previous step, and so with too many levels, the
// output can turn nearly to black.
// TODO:
// A second implementation that has the same 2 textures, but writes
// to them at alternating x/y offsets like a mip-map, and can feed
// parallel connections (like a UNet) to the shader would be better.


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
			pingPongDoFinalMerge = static_cast<Derived*>(this)->pingPongFinalMerge_vsrc.length() > 1;
			if (pingPongDoFinalMerge) {
				pingPongFinalMergeShader.compile(
						static_cast<Derived*>(this)->pingPongFinalMerge_vsrc,
						static_cast<Derived*>(this)->pingPongFinalMerge_fsrc);
			}

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
				glCheck(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texInternal[i], 0));
				checkFramebuffer();
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}

			glGenTextures(1, &texScene);
			glBindTexture(GL_TEXTURE_2D, texScene);
			// glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA, w, h);
			// glCheck(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_FLOAT, empty.data()));
			glCheck(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
			setDefaultTexParam();

			glGenTextures(1, &texSceneDepth);
			glBindTexture(GL_TEXTURE_2D, texSceneDepth);
			// glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, w, h);
			glCheck(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, empty.data()));
			glBindTexture(GL_TEXTURE_2D, 0);

			glGenFramebuffers(1, &fboScene);
			glCheck(glBindFramebuffer(GL_FRAMEBUFFER, fboScene));
			glCheck(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texScene, 0));
			// glCheck(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texSceneDepth, 0));
			checkFramebuffer();
			glCheck(glBindFramebuffer(GL_FRAMEBUFFER, 0));
		}

		inline void pingPongRender(GLuint fboFinal=0) {

			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);

			if (0) {
				// first test: just blit from scene fbo
				glCheck(glBindFramebuffer(GL_FRAMEBUFFER, fboFinal));
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

				// glViewport(0,0,w/2,h/2);

				glUseProgram(pingPongDownShader.prog);
				glCheck(glBindTexture(GL_TEXTURE_2D, texScene));
				glUniform2i(1, w, h);
				renderFullScreenQuad(1.f);
				glEnable(GL_DEPTH_TEST);
				glUseProgram(0);
				return;
			}

			int I = 1;

			levels = 4;
			int ww = this->w;
			int hh = this->h;
			float scale = 1.f;

			// ---------------------------------------------------
			// Down
			//
			glUseProgram(pingPongDownShader.prog);


			for (int i=0; i<levels; i++) {
				// auto fboSrc = fboInternal[I];
				auto fboTarget = fboInternal[1-I];
				auto texSrc = texInternal[I];
				// auto texTarget = texInternal[1-I];
				if (i == 0) texSrc = texScene;

				glCheck(glBindTexture(GL_TEXTURE_2D, texSrc));
				glCheck(glBindFramebuffer(GL_FRAMEBUFFER, fboTarget));
				glClear(GL_COLOR_BUFFER_BIT);

				// fmt::print(" - render down (I {}) (t {}) ({:>4d} {:>4d} > {:>4d} {:>4d})\n", I, fboTarget, ww,hh, ww>>1, hh>>1);
				ww >>= 1; hh >>= 1;
				glViewport(0,0,ww,hh);

				glUniform2i(1, ww, hh);
				renderFullScreenQuad(scale);
				scale *= .5f;

				
				I = 1 - I;
			}

			// ---------------------------------------------------
			// Up
			//
			glUseProgram(pingPongUpShader.prog);

			for (int i=levels-1; i>=0; i--) {
				// auto fboSrc = fboInternal[I];
				auto fboTarget = fboInternal[1-I];
				auto texSrc = texInternal[I];
				// auto texTarget = texInternal[1-I];
				if (!pingPongDoFinalMerge and i == 0) fboTarget = fboFinal;

				glCheck(glBindTexture(GL_TEXTURE_2D, texSrc));
				glBindFramebuffer(GL_FRAMEBUFFER, fboTarget);
				glClear(GL_COLOR_BUFFER_BIT);


				// fmt::print(" - render up   (I {}) (t {}) ({:>4d} {:>4d} > {:>4d} {:>4d})\n", I, fboTarget, ww,hh, ww<<1, hh<<1);
				ww <<= 1; hh <<= 1;
				glViewport(0,0,ww,hh);

				glUniform2i(1, ww, hh);
				renderFullScreenQuad(scale);
				scale *= 2.f;

				I = 1 - I;
			}


			if (pingPongDoFinalMerge) {
				glUseProgram(pingPongFinalMergeShader.prog);

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, texScene);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, texInternal[I]);
				glUniform1i(0, 0);
				glUniform1i(1, 1);
				glUniform2i(2, ww, hh);

				glBindFramebuffer(GL_FRAMEBUFFER, fboFinal);
				glClear(GL_COLOR_BUFFER_BIT);
				renderFullScreenQuad(1.f);

				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, 0);
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, 0);
			}

			glEnable(GL_DEPTH_TEST);
			glUseProgram(0);
		}

		GLuint pingPongGetSceneFbo() const { return fboScene; }

	protected:
		bool pingPongDoFinalMerge = true;

	private:
		int w, h, levels;
		Shader pingPongDownShader, pingPongUpShader;
		Shader pingPongFinalMergeShader;


		// private GLuint fboScene;
		GLuint fboInternal[2] = {0};
		GLuint texInternal[2] = {0};

		GLuint fboScene=0, texScene=0, texSceneDepth;

		inline void renderFullScreenQuad(float scale) {
			float s = scale;
			const float verts[] = {
				-1, -1,    0, 0,
				 1, -1,    s, 0,
				 1,  1,    s, s,

				 1,  1,    s, s,
				-1,  1,    0, s,
				-1, -1,    0, 0 };

			glEnableVertexAttribArray(0); // pos
			glEnableVertexAttribArray(1); //  uv
			glVertexAttribPointer(0, 2, GL_FLOAT, false, 4*4,   verts);
			glVertexAttribPointer(1, 2, GL_FLOAT, false, 4*4, 2+verts);

			glDrawArrays(GL_TRIANGLES, 0, 6);
			glDisableVertexAttribArray(0);
			glDisableVertexAttribArray(1);
		}
	
};

}
