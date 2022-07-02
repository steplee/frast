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
	vec4 anchor;
	mat4 modelMats[800];
	vec4 uvScaleAndOff[800];
} cameraData;

layout(push_constant) uniform constants {
	/* mat4 modelMat; */
	uint tileIndex;
	uint octantMask;
	uint lvl;
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

	int octant = int(aPosition.w);
	float mask = ((pushConstants.octantMask & (1<<octant)) != 0) ? 0. : 1.;
	/* mask = 1.0; */


	/* uint tileIndex = pushConstants.tileIndex; */
	uint tileIndex = gl_InstanceIndex;

	mat4 modelMat = (cameraData.modelMats[tileIndex]);
	/* mat4 modelMat = mat4(0.); */
	/* modelMat[0][0] = 1. / 255.; */
	/* modelMat[1][1] = 1. / 255.; */
	/* modelMat[2][2] = 1. / 255.; */
	/* modelMat[3][3] = 1.; */

	/* vec3 anchor = -vec3(0.17466, -0.757475,  0.627331); */

	/* mat4 modelMat = transpose(cameraData.modelMats[tileIndex]); */
	/* gl_Position = transpose(cameraData.viewProj) * (modelMat * vec4(pos, 1.0f) * mask + vec4(anchor,0.)); */
	gl_Position = transpose(cameraData.viewProj) * (modelMat * vec4(pos, 1.0f) * mask + cameraData.anchor);
	/* gl_Position = transpose(cameraData.viewProj) *  vec4(pos, 1.0f); */

	vec2 uv_scale = cameraData.uvScaleAndOff[tileIndex].xy;
	vec2 uv_off = cameraData.uvScaleAndOff[tileIndex].zw;
	v_uv = (aUv + uv_off) * uv_scale * mask;

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

	float flvl = float(pushConstants.lvl);
	flvl += pos.x / 255.0;
	/* c.r = abs(sin(flvl*.7)); */
	/* c.g = abs(sin((flvl+2.6)*1.2)); */
	/* c.b = abs(sin((flvl+9.8)*.51)); */
	/* vec3 axis = normalize(vec3(sin(flvl*6.5), sin(3.0+flvl*9.2), sin(19.3+flvl*3.3))); */
	/* float angle = sin(flvl*.97) * .7; */
	/* c = vec4(axis,angle); */

	/* float foct = float(aPosition.w); */
	/* c.r *= abs(sin(foct*7.7)); */
	/* c.b *= abs(sin(2.66+foct*3.7)); */

	v_color = c;

	v_tileIdx = tileIndex;
}


