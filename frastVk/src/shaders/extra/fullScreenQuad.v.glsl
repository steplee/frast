#version 450
precision highp float;

layout (location=0) out vec2 v_uv;

void main() {
	const vec2 ps[6] = {
		vec2( 1.,  0.),
		vec2( 0.,  1.),
		vec2( 1.,  1.),

		vec2( 0.,  1.),
		vec2( 1.,  0.),
		vec2( 0.,  0.)
		};
	vec2 p = vec2(-1.,-1.) + 2.0 * ps[gl_VertexIndex];
	gl_Position = vec4(p, .9999999, 1.);
	v_uv = ps[gl_VertexIndex];
}

