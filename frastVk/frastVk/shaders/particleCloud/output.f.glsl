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
	/* vec2 uv = v_uv * vec2(float(pushConstants.w) - 1.0, float(pushConstants.h) - 1.0); */
	/* vec2 uv = v_uv;// * pushConstants.s; */
	vec2 uv = v_uv * pushConstants.w;
	vec4 tt = textureLod(tex, uv, 0);
	float intensity = tt.r;

	float i = intensity * 1.;
	/* float a = (i + .01); */
	/* a = pow(a,3.0) + .3; */
	/* a = clamp(a, 0., .75); */
	float a = pow(tt.a,2.);

	i *= 1.;
	i = sqrt(i);
	vec4 c = vec4(i*i*i,i*i*i*i,i*i, a);
	c.r = pow(c.r,1.0);
	c.g = pow(c.g,1.0);
	/* c.a = c.a * pushConstants.d; */
	//c.g += pow(clamp(10. * (.3 - c.b), 0., 1.), 2.0) * a;
	/* c.a = length(c.rgb); */
	c.a *= pushConstants.d;
	outFragColor = clamp(c * 1., 0.,1.);
}
