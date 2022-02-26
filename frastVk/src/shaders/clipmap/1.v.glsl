#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
//#extension VK_KHR_uniform_buffer_standard_layout: enable
#extension GL_EXT_scalar_block_layout: enable

layout (location = 0) in vec3 aPositionXYI;
layout (location = 1) in vec2 aUv;
//layout (location = 2) in vec3 aNormal;

layout (location=0) out vec4 v_color;
layout (location=1) out vec2 v_uv;
layout (location=2) out flat int v_lvl;

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	uint lvl;
	float x;
	float y;
} cameraData;

//layout(packed, set=1, binding=1) uniform AltBuf {
layout(std430, set=1, binding=1) uniform AltBuf {
	float alt[512];
} altBuf[4];

layout(push_constant) uniform PushConstants {
	uint levelOffset;
	uint numLevels;
	float expansion;
} pushConstants;

/*
layout(push_constant) uniform constants {
	mat4 modelMat;
} PushConstants;
*/

void main()
{

	int index = int(aPositionXYI.z);

	//float altRaw = float(altBuf.altArr[pushConstants.levelOffset].alt[gl_VertexIndex]);
	//float altRaw = float(altBuf.altArr[0].alt[gl_VertexIndex]) / 8.0;
	//float altRaw = (altBuf.altArr[0].alt[gl_VertexIndex]);
	//float altRaw = (altBuf.altArr[pushConstants.levelOffset].alt[index]);
	//float altRaw = (altBuf[pushConstants.levelOffset].alt[index/4]);
	float altRaw = (altBuf[pushConstants.levelOffset].alt[index]);
	//float altRaw = 0.;

	// (65536/8) / (20037392.1)
	//float z_scale = 0.00040883563;
	// 1. / (8 * 20037392.1)
	//float z_scale = 0.00000000623833;
	float z_scale = 2.38418579e-7 / 8.0;
	//altRaw = float(gl_VertexIndex) / 289.0 * 8.0;
	float z = altRaw * (z_scale);
	//z = 2.38418579e-7 * .2;

	//float tilesPerLevel = 3.0;
	//float tilesPerLevel = 2.0 + 1.0/(3.0*2.0);
	//float tilesPerLevel = 2.0;
	//float levels = 4.0;

	// lvlScale increases with cameraData.lvl, but decreases with pc.levelOffset
	//float lvlScale = 1 << (cameraData.lvl - pushConstants.levelOffset);
	//float lvlScale = 1 << (pushConstants.levelOffset);
	uint level = cameraData.lvl - pushConstants.levelOffset;
	//uint level = 3 - pushConstants.levelOffset;
	float lvlScale = pow(pushConstants.expansion, level) * 2.38418579e-7; // 2.384... = 1. / (1<<22)

	//z = 0;
	//vec3 pos = vec3(aPosition2, z) * lvlScale + vec3(cameraData.x,cameraData.y,0.);
	//vec3 pos = vec3(aPosition2 * lvlScale, z) + vec3(0.);
	vec3 pos = vec3(aPositionXYI.xy * lvlScale, z) + vec3(cameraData.x, cameraData.y, 0.);
	//vec3 pos = vec3(aPositionXYI.xy * lvlScale, z) + vec3(-0.428738, 0.237438, 0.);
	//vec3 pos = vec3(aPosition2, z) * 1. + vec3(0., 0., 0.);

	/*
	const vec3 positions[4] = vec3[4](
		vec3(1.f,1.f, 0.0f),
		vec3(-1.f,1.f, 0.0f),
		vec3(0.f,-1.f, 0.0f),
		vec3(0.f,-1.f, 0.0f)
	);
	pos = positions[gl_VertexIndex];
	*/

	//output the position of each vertex
	//gl_Position = cameraData.viewProj * PushConstants.modelMat * vec4(aPosition, 1.0f);
	//gl_Position = transpose(cameraData.viewProj) * transpose(PushConstants.modelMat) * vec4(aPosition, 1.0f);
	gl_Position = transpose(cameraData.viewProj) * vec4(pos, 1.0f);

	if (pushConstants.levelOffset == 0) v_color = vec4(1., 0., 1., 1.);
	if (pushConstants.levelOffset == 1) v_color = vec4(0., 1., 0., 1.);
	if (pushConstants.levelOffset == 2) v_color = vec4(0., 0., 1., 1.);
	if (pushConstants.levelOffset == 3) v_color = vec4(0., 1., 1., 1.);
	if (pushConstants.levelOffset == 4) v_color = vec4(1., 1., .5, 1.);
	v_color = vec4(vec3(0.),1.0);
	v_color = vec4(1.0);
	v_color.rgb = vec3(float(index) / 289.0);
	//v_color.g = altRaw / 100.;
	//v_color.r = 1. * float(index) / 289.0;
	//v_color.r += altRaw;
	v_uv = aUv;
}


