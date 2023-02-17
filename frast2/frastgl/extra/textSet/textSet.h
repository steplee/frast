#pragma once

#include "frast2/frastgl/core/shader.h"




namespace frast {

	class RenderState;

	class TextSet {
		public:

			TextSet();
			~TextSet();

			void render(const RenderState& rs);

			void setText(int i, const std::string& s);
			void setTextPointingNormalToEarth(int i, const std::string& s, const float pos[3], float size, const float color[4]);
			void clear();

		private:
			constexpr static int MAX_SETS = 16;
			constexpr static int MAX_LEN = 32;

			Shader shader;

			// char txts[MAX_SETS][MAX_LEN];
			int txtLens[MAX_SETS];

			struct __attribute__((packed, aligned(16))) TextSetUniformData {
				float mat[16];
				float eye[4];
				struct __attribute__((packed, aligned(16))) InnerData {
					float innerMat[16];
					float color[4];
					uint32_t chars[MAX_LEN];
				} datas[MAX_SETS];

			} uboHost;

			GLuint ubo = 0;
			GLuint tex = 0;

	};


}
