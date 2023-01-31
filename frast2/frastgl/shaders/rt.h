#pragma once

#include <string>

namespace {

const std::string rt_tile_vsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) vec4 u_anchor;
uniform layout(location=2) sampler2D u_tex;
uniform layout(location=3) mat4 u_model;
uniform layout(location=4) vec4 u_uvScaleOff;

in layout(location=0) vec4 a_pos;
in layout(location=1) vec2 a_uv;
in layout(location=2) vec3 a_normal;

out vec2 v_uv;

void main() {
	v_uv = a_uv;

	gl_Position = u_mvp * (u_model * vec4(a_pos.xyz, 1.0) - u_anchor);

	v_uv = (u_uvScaleOff.zw + a_uv) * u_uvScaleOff.xy;
}

)";

const std::string rt_tile_fsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=2) sampler2D u_tex;

in vec2 v_uv;

out layout(location=0) vec4 out_color;

void main() {
	out_color = texture(u_tex, v_uv);
}


)";

const std::string rt_tile_casted_fsrc = R"(#version 440
)";
const std::string rt_tile_casted_vsrc = R"(#version 440
)";

}
