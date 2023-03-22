#include "textSet.h"
#include "frast2/frastgl/shaders/textSet.h"

#include "frast2/frastgl/core/render_state.h"
#include "frast2/frastgl/utils/eigen.h"
#include "frast2/frastgl/utils/check.hpp"
#include <fmt/core.h>

#include "font.hpp"

namespace frast {

			TextSet::TextSet() : shader(textSet_vsrc, textSet_fsrc) {
				// This is done in glsl by using 'layout(binding=0)'
				/*
				GLuint blockIdx = glGetUniformBlockIndex(shader.prog, "GlobalData");
				assert(blockIdx == 0);
				GLuint binding = 0;
				glUniformBlockBinding(shader.prog, blockIdx, binding);
				*/
				glGenBuffers(1, &ubo);
				glBindBuffer(GL_UNIFORM_BUFFER, ubo);
				glBufferData(GL_UNIFORM_BUFFER, sizeof(uboHost), &uboHost, GL_DYNAMIC_DRAW);
				glBindBuffer(GL_UNIFORM_BUFFER, 0);

				for (int i=0; i<MAX_SETS; i++) txtLens[i] = 0;
				bzero(&uboHost, sizeof(uboHost));
				for (int j=0; j<16; j++) uboHost.mat[j] = (j%5) == 0;
				for (int i=0; i<MAX_SETS; i++) for (int j=0; j<16; j++) uboHost.datas[i].innerMat[j] = (j%5) == 0;

				uint32_t full_width  = _texWidth * _cols;
				uint32_t full_height = _texHeight * _rows;
				// fmt::print(" - tex size {} {}\n", full_width, full_height);
				glGenTextures(1, &tex);
				glBindTexture(GL_TEXTURE_2D, tex);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, full_width, full_height, 0, GL_RED, GL_UNSIGNED_BYTE, _image);
				glBindTexture(GL_TEXTURE_2D, 0);
				
			}

			TextSet::~TextSet() {
				if (ubo) glDeleteBuffers(1, &ubo);
				if (tex) glDeleteTextures(1, &tex);
			}

			void TextSet::render(const RenderState& rs) {
				glUseProgram(shader.prog);
				// glEnableClientState(GL_VERTEX_ARRAY);
				glCheck(glActiveTexture(GL_TEXTURE0));
				glCheck(glEnable(GL_TEXTURE_2D));
				glCheck(glBindTexture(GL_TEXTURE_2D, tex));

				/*
				static int32_t verts[MAX_LEN*2];
				glVertexPointer(2, GL_INT, 0, verts);
				for (int i=0; i<MAX_SETS; i++) {
					for (int j=0; j<txtLens[i]; j++) {
						verts[i*2+0] = i;
						verts[i*2+1] = txts[i][j];
					}
				}
				*/

				/*
				static float quad6[6*2] = {
					0, 0,
					1, 0,
					1, 1,

					1, 1,
					0, 1,
					0, 0
				};
				glVertexPointer(2, GL_FLOAT, 0, verts);

				RowMatrix4f model(RowMatrix4f::Identity());
				model

				for (int i=0; i<MAX_SETS; i++) {
					for (int j=0; j<txtLens[i]; j++) {
					}
				}
				*/


				// Using UBOs

				// Use standard projection matrix
				rs.computeMvpf(uboHost.mat);
				double eye[4];
				rs.copyEye(eye);
				for (int i=0; i<3; i++) uboHost.eye[i] = eye[i];
				/*
				// for (int i=0; i<16; i++) uboHost.mat[i] = rs.view()[i];
				RowMatrix4d v;
				RowMatrix4d vv(RowMatrix4d::Identity());
				// vv.col(2) *= -1;
				RowMatrix4d p;
				RowMatrix4d mvp;
				mvp.setIdentity();
				for (int i=0; i<16; i++) v(i/4,i%4) = rs.view()[i];
				rs.camera->spec().compute_ortho(p.data());
				// rs.camera->spec().compute_projection(p.data());
				mvp = p * vv * v;
				for (int i=0; i<16; i++) uboHost.mat[i] = mvp(i);
				*/

				glCheck(glDisable(GL_CULL_FACE));

				glCheck(glBindBuffer(GL_UNIFORM_BUFFER, ubo));
				glCheck(glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(uboHost), &uboHost));
				glCheck(glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo, 0, sizeof(uboHost)));
				for (int i=0; i<MAX_SETS; i++) {
					uint32_t len = txtLens[i];
					if (len > 0) {
						// fmt::print(" - Render txt len {}\n", len);
						glUniform1i(1, i); // `replacementInstanceIdx`
						glUniform1i(2, 0); // sampler2d
						glDrawArrays(GL_TRIANGLES, 0, 6*len);
						// glDrawArrays(GL_TRIANGLES, 0, 6);
					}
				}
				glBindBuffer(GL_UNIFORM_BUFFER, 0);

				glBindTexture(GL_TEXTURE_2D, 0);
				glDisable(GL_TEXTURE_2D);
				// glDisableClientState(GL_VERTEX_ARRAY);
				glUseProgram(0);
			}

			void TextSet::clear() {
				for (int j=0; j<MAX_SETS; j++) txtLens[j] = 0;
			}

			void TextSet::setText(int i, const std::string& s) {
				assert(s.length() < MAX_LEN);
				txtLens[i] = s.length();
				for (int j=0; j<txtLens[i]; j++)
					uboHost.datas[i].chars[j] = _charIndices[s[j]];
			}


			void TextSet::setTextPointingNormalToEarth(int i, const std::string& s, const float pos_[3], float size, const float color[4]) {
				setText(i, s);

				for (int j=0; j<4; j++)
					uboHost.datas[i].color[j] = color[j];

				using namespace Eigen;
				Vector3f pos{pos_[0], pos_[1], pos_[2]};
				Map<RowMatrix4f> M { uboHost.datas[i].innerMat };
				M.topRightCorner<3,1>() = pos;
				M.block<3,1>(0,2) = pos.normalized();
				M.block<3,1>(0,0) = -pos.cross(Vector3f::UnitZ()).normalized();
				M.block<3,1>(0,1) = -M.block<3,1>(0,0).cross(M.block<3,1>(0,2)).normalized();

				// float scale = 2000 / 6e6;
				// float scale = 1;
				// float scale = .1;
				float scale = size;
				M.topLeftCorner<3,3>() *= scale;

				M.row(3) << 0,0,0,1;
			}

}
