#version 450
#extension GL_EXT_scalar_block_layout: enable

layout (location = 0) in vec4 aPosition;
layout (location = 1) in vec2 aUv;
layout (location = 2) in vec3 aNormal;

layout (location=0) out vec4 v_color;
layout (location=1) out vec2 v_uv;
layout (location=2) flat out uint v_tileIdx;

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	mat4 modelMats[256];
	vec4 uvScaleAndOff[256];
} cameraData;

layout(push_constant) uniform constants {
	/* mat4 modelMat; */
	uint tileIndex;
} pushConstants;

void main()
{

/*
	const vec3 positions[3] = vec3[3](
		vec3(1.f,1.f, 0.0f),
		vec3(-1.f,1.f, 0.0f),
		vec3(0.f,-1.f, 0.0f)
	);
	vec3 pos = positions[gl_VertexIndex%3];
	*/
	vec3 pos = aPosition.xyz;
	/* vec3 pos = vec3(0.); */


	uint tileIndex = pushConstants.tileIndex;

	mat4 modelMat = (cameraData.modelMats[tileIndex]);
	/* mat4 modelMat = mat4(0.); */
	/* modelMat[0][0] = 1. / 255.; */
	/* modelMat[1][1] = 1. / 255.; */
	/* modelMat[2][2] = 1. / 255.; */
	/* modelMat[3][3] = 1.; */

	/* mat4 modelMat = transpose(cameraData.modelMats[tileIndex]); */
	gl_Position = transpose(cameraData.viewProj) * modelMat * vec4(pos, 1.0f);
	/* gl_Position = transpose(cameraData.viewProj) *  vec4(pos, 1.0f); */

	vec2 uv_scale = cameraData.uvScaleAndOff[tileIndex].xy;
	vec2 uv_off = cameraData.uvScaleAndOff[tileIndex].zw;
	v_uv = (aUv + uv_off) * uv_scale;

	/*
	//const array of positions for the triangle
	const vec3 positions[3] = vec3[3](
		vec3(1.f,1.f, 0.0f),
		vec3(-1.f,1.f, 0.0f),
		vec3(0.f,-1.f, 0.0f)
	);
	//output the position of each vertex
	gl_Position = transpose(cameraData.viewProj) * vec4(positions[gl_VertexIndex], 1.0f);
	*/


	/* vec4 c = vec4(aUv.xxy, 1.0f); */
	vec4 c = vec4(1.0f);
	v_color = c;
	v_tileIdx = tileIndex;
}


