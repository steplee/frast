#version 450
#extension GL_EXT_scalar_block_layout: enable

layout (location = 0) in vec4 aPosition;
layout (location = 1) in vec2 aUv;
/* layout (location = 2) in vec3 aNormal; */

layout (location=0) out vec4 v_color;
layout (location=1) out vec2 v_uv;
layout (location=2) flat out uint v_tileIdx;

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	vec4 anchor;
} cameraData;

/*layout(push_constant) uniform constants {
	uint tileIndex;
	uint octantMask;
	uint lvl;
} pushConstants;*/

void main()
{
	vec3 pos = aPosition.xyz;
	v_uv = aUv;

	gl_Position = transpose(cameraData.viewProj) * (vec4(pos, 1.0f) + cameraData.anchor);

	/* vec4 c = vec4(aUv.xxy, 1.0f); */
	vec4 c = vec4(1.0f);

	v_color = c;
	/* v_tileIdx = tileIndex; */
	v_tileIdx = gl_InstanceIndex;
}
