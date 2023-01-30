#pragma once

#include <string>

namespace {

const std::string rt_tile_vsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) sampler2D u_tex;
uniform layout(location=2) mat4 u_model;
uniform layout(location=3) vec4 u_uvScaleOff;

in layout(location=0) vec4 a_pos;
in layout(location=1) vec2 a_uv;
in layout(location=2) vec3 a_normal;

out vec2 v_uv;

void main() {
	v_uv = a_uv;

	gl_Position = u_mvp * u_model * vec4(a_pos.xyz, 1.0);

	v_uv = (u_uvScaleOff.zw + a_uv) * u_uvScaleOff.xy;
}

)";

const std::string rt_tile_fsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) sampler2D u_tex;

in vec2 v_uv;

out layout(location=0) vec4 out_color;

void main() {
	out_color = texture(u_tex, v_uv);

	// out_color = vec4(texture(u_tex, v_uv).rgb,1.);
	// out_color = vec4(1.0);
	// out_color = vec4(v_uv, 1., 1.);
}


)";

const std::string rt_tile_casted_fsrc = R"(#version 440
)";
const std::string rt_tile_casted_vsrc = R"(#version 440
)";

}
