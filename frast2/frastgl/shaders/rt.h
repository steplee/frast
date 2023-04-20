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

// const std::string rt_tile_casted_fsrc = R"(#version 440
// )";
// const std::string rt_tile_casted_vsrc = R"(#version 440
// )";

const std::string rt_tile_casted_vsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) vec4 u_anchor;
uniform layout(location=2) sampler2D u_tex;
uniform layout(location=3) mat4 u_model;
uniform layout(location=4) vec4 u_uvScaleOff;
uniform layout(location=5) sampler2D u_texCast1;
uniform layout(location=6) uint u_casterMask;
uniform layout(location=7) mat4 u_casterMatrix1;
uniform layout(location=8) mat4 u_casterMatrix2;
uniform layout(location=9) vec4 u_casterColor1;
uniform layout(location=10) vec4 u_casterColor2;

in layout(location=0) vec4 a_pos;
in layout(location=1) vec2 a_uv;
in layout(location=2) vec3 a_normal;

out vec2 v_uv;
out vec4 v_caster_uv;

void main() {
	// v_uv = a_uv;
	// vec4 pos = vec4(a_pos.xyz, 1.0);
	// gl_Position = u_mvp * pos;

	// vec4 pos = (u_model * vec4(a_pos.xyz, 1.0) - u_anchor);
	vec4 pos = (u_model * vec4(a_pos.xyz, 1.0));
	gl_Position = u_mvp * (u_model * vec4(a_pos.xyz, 1.0) - u_anchor);
	v_uv = (u_uvScaleOff.zw + a_uv) * u_uvScaleOff.xy;

	//
	// Caster stuff
	//
	// Project the vertex into the caster frustum.
	// If it lies outside, make the uv = <0,0>
	if ((u_casterMask & 1u) != 0u) {
		vec4 pp = (u_casterMatrix1) * pos;
		vec3 p = pp.xyz / pp.w;
		// v_caster_uv.xy = (p.xy) * .5 + .5;
		v_caster_uv.xy = (p.xy) * .5 + .5;
	} else v_caster_uv.xy = vec2(0.0);

	if ((u_casterMask & 2) != 0u) {
		vec4 pp = (u_casterMatrix2) * pos;
		vec3 p = pp.xyz / pp.w;
		v_caster_uv.zw = (p.xy) * .5 + .5;
	} else v_caster_uv.zw = vec2(0.);

	v_caster_uv.yw = 1. - v_caster_uv.yw;
}

)";

const std::string rt_tile_casted_fsrc = R"(#version 440

uniform layout(location=0) mat4 u_mvp;
uniform layout(location=1) vec4 u_anchor;
uniform layout(location=2) sampler2D u_tex;
uniform layout(location=3) mat4 u_model;
uniform layout(location=4) vec4 u_uvScaleOff;
uniform layout(location=5) sampler2D u_texCast1;
uniform layout(location=6) uint u_casterMask;
uniform layout(location=7) mat4 u_casterMatrix1;
uniform layout(location=8) mat4 u_casterMatrix2;
uniform layout(location=9) vec4 u_casterColor1;
uniform layout(location=10) vec4 u_casterColor2;

in vec2 v_uv;
in vec4 v_caster_uv;

out layout(location=0) vec4 out_color;

void main() {

	vec4 outColor = vec4(0.);

	outColor = vec4(texture(u_tex, v_uv).rgb,1.);

	// const vec4 color1 = vec4(0.,1.,0.,1.);
	// const vec4 color2 = vec4(0.,0.,1.,1.);

	//
	// Caster stuff
	//
	// Note: casted images are weighted 3x tile texture
	vec2 uv_c1 = v_caster_uv.xy;
	vec2 dd1 = abs(uv_c1 - .5);
	float d1 = max(dd1.x , dd1.y);
	if (d1 < .49999) {
		vec4 c = texture(u_texCast1, uv_c1) * u_casterColor1;
		// vec4 c = vec4(fract(uv_c1.x),0.,0.,1.);
		// vec4 c = vec4(uv_c1.x,0.,0.,1.);
		// c *= 3.0 * clamp(2.0 - 4. * (d1), 0., 1.);
		outColor += c;
	}

	vec2 uv_c2 = v_caster_uv.zw;
	vec2 dd2 = abs(uv_c2 - .5);
	float d2 = max(dd2.x , dd2.y);
	/* if (v_caster_uv.zw != vec2(0.)) { */
	if (d2 < .49999) {
		vec4 c = texture(u_texCast1, uv_c2) * u_casterColor2;
		c *= 3.0 * clamp(2.0 - 4. * (d2), 0., 1.);
		outColor += c;
	}


	outColor.rgba /= outColor.a;
	out_color = outColor;

}

)";

}
