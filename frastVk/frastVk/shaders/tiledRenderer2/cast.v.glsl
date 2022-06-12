#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_scalar_block_layout: enable
//#extension VK_KHR_uniform_buffer_standard_layout: enable

layout (location = 0) in vec3 aPositionXYZI;
layout (location = 1) in vec2 aUv;
//layout (location = 2) in vec3 aNormal;

layout (location=0) out vec4 v_color;
layout (location=1) out vec2 v_uv;
layout (location=2) out flat uint v_tileId;
layout (location=3) out vec4 v_caster_uv; // uv1, uv2

// Note: UBOs cannot be std430 layout, but with the GL_EXT_scalar_block_layout extension,
//       this should be packed correctly.
layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	uint tileIds[256];
} cameraData;

layout(std430, set=2, binding=0) uniform CasterData {
	mat4 casterMatrix[2];
	vec4 color1;
	vec4 color2;
	uint casterMask;
} casterData;

void main()
{
	uint tileIndex = cameraData.tileIds[gl_InstanceIndex];


	vec3 pos = aPositionXYZI.xyz;

	gl_Position = transpose(cameraData.viewProj) * vec4(pos, 1.0f);

	v_color = vec4(1.0);

	v_uv = aUv;
	v_tileId = tileIndex;

	//
	// Caster stuff
	//
	// Project the vertex into the caster frustum.
	// If it lies outside, make the uv = <0,0>
	if ((casterData.casterMask & 1) != 0u) {
		vec4 pp = transpose(casterData.casterMatrix[0]) * vec4(pos, 1.0f);
		vec3 p = pp.xyz / pp.w;
		/* if (p.z < 0 || p.z > 1 || p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1) v_caster_uv.xy = vec2(0.); */
		/* else v_caster_uv.xy = p.xy; */
		/* v_caster_uv.xy = clamp(p.xy, 0., 1.); */
		/* v_caster_uv.xy = (p.xy * vec2(1.,-1.)) * .5 + .5; */
		/* v_caster_uv.xy = (p.xy * vec2(-1., 1.)) * .5 + .5; */
		v_caster_uv.xy = (p.xy) * .5 + .5;
	} else v_caster_uv.xy = vec2(0.);

	if ((casterData.casterMask & 2) != 0u) {
		vec4 pp = transpose(casterData.casterMatrix[1]) * vec4(pos, 1.0f);
		vec3 p = pp.xyz / pp.w;
		/* if (p.z < 0 || p.z > 1 || p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1) v_caster_uv.xy = vec2(0.); */
		/* else v_caster_uv.xy = p.xy; */
		/* v_caster_uv.xy = clamp(p.xy, 0., 1.); */
		/* v_caster_uv.zw = (p.xy * vec2(1.,-1.)) * .5 + .5; */
		/* v_caster_uv.zw = (p.xy * vec2(-1., 1.)) * .5 + .5; */
		v_caster_uv.zw = (p.xy) * .5 + .5;
	} else v_caster_uv.zw = vec2(0.);

}
