#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_scalar_block_layout: enable
//#extension VK_KHR_uniform_buffer_standard_layout: enable

layout (location = 0) in vec3 aPositionXYI;
layout (location = 1) in vec2 aUv;
//layout (location = 2) in vec3 aNormal;

layout (location=0) out vec4 v_color;
layout (location=1) out vec2 v_uv;
layout (location=2) out flat uint v_tileId;

// Note: UBOs cannot be std430 layout, but with the GL_EXT_scalar_block_layout extension,
//       this should be packed correctly.
layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	uint tileIds[128];
} cameraData;

layout(std430, set=1, binding=1) readonly buffer AltBuf {
	uint x;
	uint y;
	uint z;
	uint pad;
	float alt[64];
} altBuf[128];

/*
layout(push_constant) uniform PushConstants {
	int tileIdx;
} pushConstants;
*/

void main()
{
	//int tileIndex = pushConstants.tileIdx;
	uint tileIndex = cameraData.tileIds[gl_InstanceIndex];
	//uint tileIndex = cameraData.tileIds[2];
	int vertIndex = int(aPositionXYI.z);

	float altRaw = (altBuf[tileIndex].alt[vertIndex]);
	//altRaw = 0.;
	float z = altRaw;

	//float z_scale = 2.38418579e-7 / 8.0;
	//float z = altRaw * (z_scale);
	//float z = 0.0;

	uint tx = altBuf[tileIndex].x,
		 ty = altBuf[tileIndex].y,
		 tz = altBuf[tileIndex].z;
	float lvlScale = 2.0 / float(1 << tz);
	//vec3 lvlOffset = vec3(0.);
	vec3 lvlOffset = vec3(
		(float(tx) / float(1<<(tz))) * 2. - 1.,
		(float(ty) / float(1<<(tz))) * 2. - 1.,
		0.0);
		

	//vec3 pos = vec3(aPositionXYI.xy * .1 - vec2(.43,-.22), z) + lvlOffset;
	vec3 pos = vec3(aPositionXYI.xy * lvlScale, z) + lvlOffset;

	gl_Position = transpose(cameraData.viewProj) * vec4(pos, 1.0f);

	v_color = vec4(1.0);
	//v_color.r = aPositionXYI.x;
	//v_color.g = aPositionXYI.y;
	//v_color.g = lvlOffset.y;

	v_uv = aUv;
	v_tileId = tileIndex;
}



