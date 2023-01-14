#pragma once

#include <string>

namespace {

const std::string ft_tile_vsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) sampler2D u_tex;

in layout(location=0) vec4 a_pos;
in layout(location=1) vec2 a_uv;

out vec2 v_uv;

void main() {
	v_uv = a_uv;

	gl_Position = u_mvp * vec4(a_pos.xyz, 1.0);
	// gl_Position = vec4(.0,.0,.0,1.);
}

)";


const std::string ft_tile_fsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) sampler2D u_tex;

in vec2 v_uv;

out layout(location=0) vec4 out_color;

void main() {
	// out_color = texture(u_tex, v_uv);

	out_color = .1 + vec4(texture(u_tex, v_uv).rgb,1.);

	// out_color = vec4(1.0);
}

)";




}
