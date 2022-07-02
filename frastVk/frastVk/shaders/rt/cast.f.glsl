#version 450
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) in vec4 v_color;
layout (location=1) in vec2 v_uv;
layout (location=2) flat in uint v_tileIndex;
layout (location=3) in vec4 v_caster_uv; // uv1, uv2

layout(set = 1, binding = 0) uniform sampler2D texs[800];
layout(set=2, binding=1) uniform sampler2D casterTex[1];

layout (location = 0) out vec4 outFragColor;

layout(std430, set=2, binding=0) uniform CasterData {
	mat4 casterMatrix[2];
	vec4 color1;
	vec4 color2;
	uint casterMask;
} casterData;

void main()
{
	vec4 final_color = texture(texs[v_tileIndex], v_uv) * v_color;
	final_color.a = 1.0;

	//
	// Caster stuff
	//
	// Note: casted images are weighted 3x tile texture
	vec2 uv_c1 = v_caster_uv.xy;
	vec2 dd1 = abs(uv_c1 - .5);
	float d1 = max(dd1.x , dd1.y);
	if (d1 < .49999) {
		/* vec4 c = texture(casterTex[0], uv_c1) * vec4(0.7, 0.7, 1., 1.); */
		vec4 c = texture(casterTex[0], uv_c1) * casterData.color1;
		c *= 3.0 * clamp(2.0 - 4. * (d1), 0., 1.);
		final_color += c;
	}

	vec2 uv_c2 = v_caster_uv.zw;
	vec2 dd2 = abs(uv_c2 - .5);
	float d2 = max(dd2.x , dd2.y);
	/* if (v_caster_uv.zw != vec2(0.)) { */
	if (d2 < .49999) {
		/* vec4 c = texture(casterTex[0], uv_c2) * vec4(0.7, 1., .7, 1.); */
		vec4 c = texture(casterTex[0], uv_c2) * casterData.color2;
		c *= 3.0 * clamp(2.0 - 4. * (d2), 0., 1.);
		final_color += c;
	}


	final_color.rgba /= final_color.a;
	outFragColor = final_color;
}


