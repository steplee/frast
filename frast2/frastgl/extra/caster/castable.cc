#include "castable.h"

namespace frast {

		void Castable::setCasterInRenderThread() {
			if (not cwd.isNew()) return;

			casterStuff.casterMask = cwd.mask;

			if (casterStuff.lastTexSize == 0) {
				uint32_t texSize = cwd.image.total();
				casterStuff.lastTexSize = texSize;

				auto fmt = cwd.image.channels() == 4 ? GL_RGBA : cwd.image.channels() == 3 ? GL_RGB : GL_LUMINANCE;
				glBindTexture(GL_TEXTURE_2D, casterStuff.tex);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cwd.image.cols, cwd.image.rows, 0, fmt, GL_UNSIGNED_BYTE, cwd.image.data);
				glBindTexture(GL_TEXTURE_2D, 0);

			} else {
				uint32_t texSize = cwd.image.total();
				assert(casterStuff.lastTexSize == texSize);

				auto fmt = cwd.image.channels() == 4 ? GL_RGBA : cwd.image.channels() == 3 ? GL_RGB : GL_LUMINANCE;
				glBindTexture(GL_TEXTURE_2D, casterStuff.tex);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0,0, cwd.image.cols, cwd.image.cols, fmt, GL_UNSIGNED_BYTE, cwd.image.data);
				glBindTexture(GL_TEXTURE_2D, 0);
			}

			if (cwd.haveMatrix1) memcpy(casterStuff.cpuCasterBuffer.casterMatrix1, cwd.casterMatrix1, 4*16);
			if (cwd.haveMatrix2) memcpy(casterStuff.cpuCasterBuffer.casterMatrix2, cwd.casterMatrix2, 4*16);
			if (cwd.haveColor1) memcpy(casterStuff.cpuCasterBuffer.color1, cwd.color1, 4*4);
			if (cwd.haveColor2) memcpy(casterStuff.cpuCasterBuffer.color2, cwd.color2, 4*4);


		}

		void Castable::do_init_caster_stuff() {
			glGenTextures(1, &casterStuff.tex);
			glBindTexture(GL_TEXTURE_2D, casterStuff.tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D, 0);

		}

		Castable::~Castable() {
			if (casterStuff.tex != 0)
				glDeleteTextures(1, &casterStuff.tex);
		}

}
