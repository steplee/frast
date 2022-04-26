#version 450
precision highp float;

layout (location=0) out vec2 v_uv;

layout(push_constant) uniform PushConstants {
	uint w;
	uint h;
	float s;
} pushConstants;

void main() {
	const vec2 ps[6] = {
		vec2( 1.,  0.),
		vec2( 0.,  1.),
		vec2( 1.,  1.),

		vec2( 0.,  1.),
		vec2( 1.,  0.),
		vec2( 0.,  0.)
		};
	vec2 p = vec2(-1.,-1.) + 2.0 * pushConstants.s * ps[gl_VertexIndex];
	gl_Position = vec4(p, 0., 1.);
	/* v_uv = (p + 1.0) * .5; */
	v_uv = ps[gl_VertexIndex];
}
