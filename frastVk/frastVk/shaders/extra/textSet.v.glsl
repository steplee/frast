#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_8bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8: enable
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) out vec2 v_uv;
layout (location=1) out vec4 v_color;

// Size of each glyph
const float w = 20.;
const float h = 32.;


/*
Specifically desigend for a 16-column wide texture of 32x20 pixels each glpyh
Max strings: 32
*/

struct Data {
	mat4 matrix;
	vec4 color;
	uint8_t chars[176];
};
layout(std430, set=0, binding=0) uniform GlobalData {
	//vec2 offset;
	//vec2 windowSize;
	mat4 matrix;
	/* vec2 size; */
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
	
	/*
	const float SPACING = .82;
	vec2 localOffset = vec2(float(index), 0.) * SPACING;
	p = (p + localOffset) * vec2(w/h) * 20.;
	vec4 pp = transpose(ubo.datas[gl_InstanceIndex].matrix) * vec4(p,0.,1.);
	pp.xy *= 2. / ubo.windowSize;
	pp.xy += ubo.offset - vec2(1.,1.);
	pp.z = .0001;
	pp.w = 1.0;
	*/

	const float SPACING = .82;
	vec2 localOffset = vec2(float(index), 0.) * SPACING;
	/* p = (p + localOffset) * vec2(w/h) * 1.; */
	p = (p + localOffset);
	vec4 pp = transpose(ubo.matrix) * transpose(ubo.datas[gl_InstanceIndex].matrix) * vec4(p,0.,1.);
	/* pp.xy *= 10.; */
	/* pp.z = .0001; */
	/* pp.z = .01; */
	/* pp.w = 1.0; */


	gl_Position = pp;

}
