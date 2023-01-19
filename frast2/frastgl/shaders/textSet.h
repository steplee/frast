#pragma once

#include <string>

namespace {


const std::string textSet_vsrc = R"(#version 440
precision mediump float;

layout (location=0) out vec2 v_uv;
layout (location=1) out vec4 v_color;

// Size of each glyph
const float w = 20.;
const float h = 32.;
const float ww = 320.;
const float hh = 160.;

struct Data {
	mat4 matrix;
	vec4 color;
	uint chars[32];
};
// layout(std430, set=0, binding=0) uniform GlobalData {
layout(location=0,binding=0) uniform GlobalData {
	mat4 matrix;
	vec4 eye;
	Data datas[32];
} ubo;

layout(location=1) uniform int replacementInstanceIdx;

void main() {

	/*
	const vec2 ps[6] = {
		vec2( 1.,  0.),
		vec2( 0.,  1.),
		vec2( 1.,  1.),

		vec2( 0.,  1.),
		vec2( 1.,  0.),
		vec2( 0.,  0.)
		};

	vec2 p = ps[gl_VertexIndex % 6];

	int index = gl_VertexIndex / 6;

	uint c = uint(ubo.datas[gl_InstanceIndex].chars[index]);

	float row = float(c / 16);
	float col = float(c % 16);

	v_uv = vec2((col+p.x) * w, (row+p.y) * h);
	v_color = ubo.datas[gl_InstanceIndex].color;

	const float SPACING = .82;
	vec2 localOffset = vec2(float(index), 0.) * SPACING;

	p = (p + localOffset);
	vec4 pp = transpose(ubo.matrix) * transpose(ubo.datas[gl_InstanceIndex].matrix) * vec4(p,0.,1.);


	gl_Position = pp;
	*/

	const vec2 ps[6] = {
		vec2( 0.,  0.),
		vec2( 1.,  0.),
		vec2( 1.,  1.),

		vec2( 1.,  1.),
		vec2( 0.,  1.),
		vec2( 0.,  0.)
		};

	vec2 p = ps[gl_VertexID % 6];
	int index = gl_VertexID / 6;
	// uint c = uint(ubo.datas[gl_InstanceID].chars[index]);

	uint c = uint(ubo.datas[replacementInstanceIdx].chars[index]);
	// c = 0;

	float row = float(c / 16);
	float col = float(c % 16);

	// v_uv = vec2((col+p.x) * w/ww, (row+p.y) * h/hh);
	v_uv = vec2((col+p.x) * w/ww, (row+1.-p.y) * h/hh);
	// v_uv = p;
	// v_uv = vec2((col+p.x) * (w-1.)/ww, (row+p.y) * (h-1.)/hh);

	// v_color = ubo.datas[replacementInstanceIdx].color;
	v_color = vec4(1.);

	const float SPACING = .82;
	vec2 localOffset = vec2(float(index), 0.) * SPACING;


	p = (p + localOffset);

	float z = float(index)*.00001;
	// vec4 p1 = transpose(ubo.datas[replacementInstanceIdx].matrix) * vec4(p, z, 1.);
	vec4 p1 = transpose(ubo.datas[replacementInstanceIdx].matrix) * vec4(.5,.5, z, 1.);

	float dist = distance(ubo.eye.xyz, p1.xyz/p1.z);
	float scale = 1./dist;
	scale = dist;

	// p1 = vec4(p1.xyz / p1.w, 1.);
	// p1.xyz *= scale;
	p1 = transpose(ubo.datas[replacementInstanceIdx].matrix) * vec4(scale*p, z, 1.);
	vec4 pp = transpose(ubo.matrix) *  p1;

	// vec4 pp = vec4(p,0.,1.);

	gl_Position = pp;
	// gl_Position = vec4(p*2.-1.,.5,1.);
	

})";

const std::string textSet_fsrc = R"(#version 440
precision mediump float;
layout (location=0) in vec2 v_uv;
layout (location=1) in vec4 v_color;
// layout(set = 0, binding = 1) uniform sampler2D tex;
layout(location=2) uniform sampler2D tex;

layout (location = 0) out vec4 outFragColor;

void main()
{

	vec4 final_color;
	final_color = v_color * textureLod(tex, v_uv, 0).rrrr;
	// final_color = v_color * texture(tex, v_uv);
	/* final_color += vec4(v_uv / 320., 0.,.1); */

	// final_color.r = 1.;
	// final_color.a += .1;

	outFragColor = final_color;
})";

}
