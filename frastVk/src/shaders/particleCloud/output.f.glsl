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
	/* vec2 uv = v_uv * vec2(float(pushConstants.w) - 1.0, float(pushConstants.h) - 1.0); */
	vec2 uv = v_uv * vec2(float(pushConstants.w), float(pushConstants.h));
	float intensity = textureLod(tex, uv, 0).r;

	float i = intensity * 1.;
	float a = (i + .01);
	a = pow(a,3.0) + .3;
	a = clamp(a, 0., .75);

	i *= 1.;
	i = sqrt(i);
	vec4 c = vec4(i*i*i,i*i*i*i,i*i, a);
	c.r = pow(c.r,4.0);
	c.g = pow(c.g,2.0);
	//c.g += pow(clamp(10. * (.3 - c.b), 0., 1.), 2.0) * a;
	outFragColor = c * 1.;
}
