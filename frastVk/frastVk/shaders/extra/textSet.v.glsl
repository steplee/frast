#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_8bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8: enable
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) out vec2 v_uv;


/*
Specifically desigend for a 16-column wide texture of 32x20 pixels each glpyh
Max strings: 32
*/

struct Data {
	mat4 matrix;
	uint8_t chars[132];
};
layout(std430, set=0, binding=0) uniform GlobalData {
	//mat4 matrix;
	//uint8_t chars[128];
	Data datas[32];
} ubo;
/*
layout(std430, set=0, binding=0) uniform GlobalData {
	mat4 matrix[32];
	uint8_t chars[4096];
} ubo;
*/

void main() {

	const float size = .05;

	const vec2 ps[6] = {
		vec2( 1.,  0.),
		vec2( 0.,  1.),
		vec2( 1.,  1.),

		vec2( 0.,  1.),
		vec2( 1.,  0.),
		vec2( 0.,  0.)
		};

	vec2 p = 1.0 * ps[gl_VertexIndex % 6];

	int index = gl_VertexIndex / 6;

	uint c = uint(ubo.datas[gl_InstanceIndex].chars[index]);
	/* uint c = uint(ubo.chars[gl_InstanceIndex*128 + index/6]); */
	/* uint c = index; */

	float row = float(c / 16);
	float col = float(c % 16);

	v_uv = vec2((col+p.x) * 20., (row+p.y) * 32.);
	/* v_uv = ps[gl_VertexIndex]; */
	
	vec2 localOffset = .9 * vec2(float(index), 0.);
	p = (p + localOffset) * size;
	vec4 pp = ubo.datas[gl_InstanceIndex].matrix * vec4(p,0.,1.);
	/* vec4 pp = ubo.matrix[gl_InstanceIndex] * vec4(p,0.,1.); */

	pp.z = .0001;
	pp.w = 1.0;

	gl_Position = pp;

}
