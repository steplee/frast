#version 450
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) in vec2 v_uv;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout (location = 0) out vec4 outFragColor;

layout(std430, push_constant) uniform PushConstants {
	float w;
	float h;
	float s;
	float d;
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


/*
	vec2 uv = v_uv * vec2(float(2*pushConstants.w), float(2*pushConstants.h)) + 1.0;
	vec4 c = vec4(0.);
	vec2 duv = 1.0 * vec2(1.0, 1.0);
	c += 2.0 * textureLod(tex, uv + duv * vec2(0.), 0);
	float z = -2., o = 2.;
	c += textureLod(tex, uv + duv * vec2(z,z), 0);
	c += textureLod(tex, uv + duv * vec2(o,z), 0);
	c += textureLod(tex, uv + duv * vec2(o,o), 0);
	c += textureLod(tex, uv + duv * vec2(z,o), 0);
	outFragColor = clamp(c * .5, 0., 1.);
	*/

	/* vec2 uv = v_uv * vec2(float(2*pushConstants.w), float(2*pushConstants.h)) + 0.0; */
	/* vec2 uv = v_uv * vec2(float(2*pushConstants.w), float(2*pushConstants.h)) + vec2(0.,0.); */
	/* vec2 uv = v_uv * vec2(float(2*pushConstants.w-1), float(2*pushConstants.h-1)) + 1.0; */
	vec2 uv = v_uv;
	/* uv.y = 1.0 - uv.y; */
	uv = uv * pushConstants.s * 2. + .0 / pushConstants.w;
	vec4 c = vec4(0.);
	vec2 duv = .0 * vec2(1.0, 1.0);
	c += 1.0 * textureLod(tex, uv + duv * vec2(0.), 0);
	outFragColor = clamp(c * 2.2, 0., 1.);
}
