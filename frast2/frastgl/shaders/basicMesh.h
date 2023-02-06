#pragma once

#include <string>

namespace {

const std::string basicMeshWithTex_vsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) sampler2D u_tex;

in layout(location=0) vec4 a_pos;
in layout(location=1) vec2 a_uv;
in layout(location=2) vec3 a_normal;

out vec2 v_uv;
out vec3 v_normal;

void main() {
	v_uv = a_uv;
	v_normal = a_normal;

	gl_Position = u_mvp * vec4(a_pos.xyz, 1.0);
}

)";


const std::string basicMeshWithTex_fsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) sampler2D u_tex;

in vec2 v_uv;
in vec3 v_normal;

out layout(location=0) vec4 out_color;

void main() {
	out_color = vec4(texture(u_tex, v_uv).rgb,1.);
}

)";


const std::string basicMeshNoTex_vsrc = R"(#version 440

uniform layout(location=0) mat4 u_proj;
uniform layout(location=1) mat4 u_mv;

in layout(location=0) vec4 a_pos;
in layout(location=1) vec2 a_uv;
in layout(location=2) vec3 a_normal;

out vec2 v_uv;
out vec3 v_normal;

void main() {
	v_uv = a_uv;
	v_normal = a_normal;
	// v_normal = vec3(0.,0., dot(a_normal, -normalize(vec3(u_mv[0][2], u_mv[1][2], u_mv[2][2]))));

	// gl_Position = u_mvp * vec4(a_pos.xyz, 1.0);
	gl_Position = u_proj * u_mv * vec4(a_pos.xyz, 1.0);
}

)";


const std::string basicMeshNoTex_fsrc = R"(#version 440

uniform layout(location=0) mat4 u_proj;
uniform layout(location=1) mat4 u_mv;

in vec2 v_uv;
in vec3 v_normal;

out layout(location=0) vec4 out_color;

void main() {

	vec3 zn = -normalize(vec3(u_mv[0][2], u_mv[1][2], u_mv[2][2]));

	// float s = sqrt(abs(dot(v_normal, zn)));
	float s = sqrt(max(.000001,dot(v_normal, zn)));
	// float s = v_normal.z;

	vec3 col = vec3(s);
	// vec3 col = s*abs(v_normal);

	out_color = vec4(col, 1.);
}

)";

}
