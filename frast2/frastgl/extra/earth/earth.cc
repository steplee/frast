#include "earth.h"

#include "frast2/frastgl/core/render_state.h"
#include "frast2/frastgl/core/shader.h"
#include "frast2/frastgl/shaders/earth.h"

namespace frast {

EarthEllipsoid::EarthEllipsoid()
	: shader(earth_ellps_vsrc, earth_ellps_fsrc)
{

}

void EarthEllipsoid::render(const RenderState& rs) {

	float viewInv[16];
	// invertMatrix44(i_mvpf, mvpf);

	for (int i=0; i<16; i++) viewInv[i] = static_cast<float>(rs.viewInv()[i]);
	for (int i=0; i<3; i++) viewInv[i*4+0] *= .5 * rs.camera->spec().w / rs.camera->spec().fx();
	for (int i=0; i<3; i++) viewInv[i*4+1] *= .5 * rs.camera->spec().h / rs.camera->spec().fy();

	glUseProgram(shader.prog);

	glUniformMatrix4fv(0, 1, true, viewInv);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glUseProgram(0);

}

}
