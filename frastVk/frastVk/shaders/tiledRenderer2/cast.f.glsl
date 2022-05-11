#version 450
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) in vec4 v_color;
layout (location=1) in vec2 v_uv;
layout (location=2) in flat uint v_tileId;
layout (location=3) in vec4 v_caster_uv; // uv1, uv2

layout(set = 1, binding = 0) uniform sampler2D tex[128];

/*
layout(std430, set=2, binding=0) uniform CasterData {
	mat4 casterMatrix[2];
	uint casterMask[2];
} casterData;
*/
layout(set=2, binding=1) uniform sampler2D casterTex[1];

//output write
layout (location = 0) out vec4 outFragColor;

/*
layout (push_constant) uniform PushConstants {
	bool grayscale;
} pushConstants;
*/

void main()
{
	//return red

	vec4 color = v_color;

	vec4 final_color;
	/* if (pushConstants.grayscale) final_color = color * texture(tex[v_tileId], v_uv).rrra; */
	/* else final_color = color * texture(tex[v_tileId], v_uv); */
	final_color = color * texture(tex[v_tileId], v_uv);

	if (final_color.r + final_color.g + final_color.b < .01) final_color.a = 0.;

	/* if (v_caster_uv.xy != vec2(0.)) { */

	//
	// Caster stuff
	//
	// Note: casted images are weighted 3x tile texture
	vec2 uv_c1 = v_caster_uv.xy;
	vec2 dd1 = abs(uv_c1 - .5);
	float d1 = max(dd1.x , dd1.y);
	if (d1 < .4999) {
		vec4 c = texture(casterTex[0], uv_c1) * vec4(0.7, 0.7, 1., 1.);
		/* final_color = (final_color * (1-c.a)) + (c.a) * vec4(c.rgb,1.0); */
		c *= 3.0 * clamp(2.0 - 4. * (d1), 0., 1.);
		final_color += c;
	}

	vec2 uv_c2 = v_caster_uv.zw;
	vec2 dd2 = abs(uv_c2 - .5);
	float d2 = max(dd2.x , dd2.y);
	/* if (v_caster_uv.zw != vec2(0.)) { */
	if (d2 < .4999) {
		vec4 c = texture(casterTex[0], uv_c2) * vec4(0.7, 1., .7, 1.);
		/* final_color = (final_color * (1-c.a)) + (c.a) * vec4(c.rgb,1.0); */
		c *= 3.0 * clamp(2.0 - 4. * (d2), 0., 1.);
		final_color += c;
	}


	//if (abs(.5 - v_uv.x) > .492 || abs(.5 - v_uv.y) > .492) final_color.b = 1.0;

	final_color /= final_color.a;
	outFragColor = final_color;
}

