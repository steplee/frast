#version 450
precision highp float;
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) out vec2 v_uv;

layout(std430, push_constant) uniform PushConstants {
	vec4 whsd;
	/* float w; */
	/* float h; */
	/* float s; */
	/* float d; */
} pushConstants;

void main() {
	float w = pushConstants.whsd[0], h = pushConstants.whsd[1], s = pushConstants.whsd[2], d = pushConstants.whsd[3];

	const vec2 ps[6] = {
		vec2( 1.,  0.),
		vec2( 0.,  1.),
		vec2( 1.,  1.),

		vec2( 0.,  1.),
		vec2( 1.,  0.),
		vec2( 0.,  0.)
		};
	vec2 p = vec2(-1.,-1.) + 2.0 * s * (1. - ps[gl_VertexIndex]);
	/* gl_Position = vec4(p, 0.9 - (pushConstants.w*.0001), 1.); */
	/* gl_Position = vec4(p, pushConstants.d, 1.); */
	gl_Position = vec4(p, .999, 1.);
	/* v_uv = (p + 1.0) * .5; */
	v_uv = 1. - ps[gl_VertexIndex];
}
