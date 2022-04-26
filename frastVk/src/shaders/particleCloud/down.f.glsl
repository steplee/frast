#version 450

layout (location=0) in vec2 v_uv;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout (location = 0) out vec4 outFragColor;

layout(push_constant) uniform PushConstants {
	uint w;
	uint h;
	float s;
} pushConstants;

void main() {
	/* vec2 uv = v_uv * 2.0; */
	/*
	//vec2 uv = v_uv * vec2(2.0 - 1.0 / pushConstants.w, 2.0 - 1.0 / pushConstants.h);
	//uv += 1. * vec2(1.0/pushConstants.w, 1.0/pushConstants.h);
	vec4 c = texture(tex, uv) * 2.0;
	outFragColor = c;
	*/

	/*
	vec4 c = vec4(0.);
	vec2 duv = .125 * vec2(1.0/pushConstants.w, 1.0/pushConstants.h);
	c += texture(tex, uv + duv * vec2(0.));
	c += texture(tex, uv + duv * vec2(-1., -1.));
	c += texture(tex, uv + duv * vec2( 1., -1.));
	c += texture(tex, uv + duv * vec2( 1.,  1.));
	c += texture(tex, uv + duv * vec2(-1.,  1.));
	outFragColor = c * .2;
	*/

	/*
	uv *= 1.0 - .5 * vec2(1.0/pushConstants.w, 1.0/pushConstants.h);
	uv -= .5 * vec2(1.0/pushConstants.w, 1.0/pushConstants.h);

	vec4 c = vec4(0.);
	vec2 duv = .5 * vec2(1.0/pushConstants.w, 1.0/pushConstants.h);
	c += texture(tex, uv + duv * vec2( 0., -0.));
	c += texture(tex, uv + duv * vec2( 1., -0.));
	c += texture(tex, uv + duv * vec2( 1.,  1.));
	c += texture(tex, uv + duv * vec2( 0.,  1.));
	outFragColor = c * .3;
	*/


	vec2 uv = v_uv * vec2(float(2*pushConstants.w), float(2*pushConstants.h)) + 1.0;
	/* vec2 uv = v_uv * vec2(float(2*pushConstants.w) - 1.0, float(2*pushConstants.h) - 1.0); */
	vec4 c = vec4(0.);
	vec2 duv = 1.0 * vec2(1.0, 1.0);
	c += 4.0 * textureLod(tex, uv + duv * vec2(0.), 0);
	c += textureLod(tex, uv + duv * vec2(-1., -1.), 0);
	c += textureLod(tex, uv + duv * vec2( 1., -1.), 0);
	c += textureLod(tex, uv + duv * vec2( 1.,  1.), 0);
	c += textureLod(tex, uv + duv * vec2(-1.,  1.), 0);
	/* c.r = (sin(.5*uv.y)) * 5.; */
	/* c.r = v_uv.y * 5.; */
	outFragColor = clamp(c * .2, 0., 1.);
}
