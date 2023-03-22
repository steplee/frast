#pragma once

#include "check.hpp"

namespace frast {

struct BlendGuard {
	bool wasEnabled;
	int oldSrc, oldDst;

	inline BlendGuard(bool enable, int src, int dst) {
		bool wasEnabled = glIsEnabled(GL_BLEND);
		if (wasEnabled) {
			glGetIntegerv(GL_BLEND_SRC_RGB, &oldSrc);
			glCheck("B");
			glGetIntegerv(GL_BLEND_DST_RGB, &oldDst);
		}
		if (enable) {
			glEnable(GL_BLEND);
			glCheck(glBlendFunc(src,dst));
		} else {
			glDisable(GL_BLEND);
		}
	}

	inline ~BlendGuard() {
		if (wasEnabled) glEnable(GL_BLEND);
		else            glDisable(GL_BLEND);
		glBlendFunc(oldSrc,oldDst);
	}
};

}
