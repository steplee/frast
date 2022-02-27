#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_scalar_block_layout: enable

layout (location = 0) in vec3 aPositionXYI;
layout (location = 1) in vec2 aUv;
//layout (location = 2) in vec3 aNormal;

layout (location=0) out vec4 v_color;
layout (location=1) out vec2 v_uv;
layout (location=2) out flat int v_lvl;

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	int lvl;
	float x;
	float y;
} cameraData;

layout(std430, set=1, binding=1) uniform AltBuf {
	float alt[512];
} altBuf[4];

layout(push_constant) uniform PushConstants {
	int levelOffset;
	int numLevels;
	float expansion;
} pushConstants;

void main()
{
	int index = int(aPositionXYI.z);

	float altRaw = (altBuf[pushConstants.levelOffset].alt[index]);

	float z_scale = 2.38418579e-7 / 8.0;
	float z = altRaw * (z_scale);

	// lvlScale increases with cameraData.lvl, but decreases with pc.levelOffset
	//int level = cameraData.lvl - pushConstants.levelOffset;
	//float lvlScale = pow(pushConstants.expansion, level) * 0.00000762939; // 2.384... = 1. / (1<<17)
	float lvlScale = pow(pushConstants.expansion, -cameraData.lvl-pushConstants.levelOffset+pushConstants.numLevels);

	//z = 0;
	//vec3 pos = vec3(aPosition2, z) * lvlScale + vec3(cameraData.x,cameraData.y,0.);
	//vec3 pos = vec3(aPosition2 * lvlScale, z) + vec3(0.);
	vec3 pos = vec3(aPositionXYI.xy * lvlScale, z) + vec3(cameraData.x, cameraData.y, 0.);
	//vec3 pos = vec3(aPositionXYI.xy * lvlScale, z) + vec3(-0.428738, 0.237438, 0.);
	//vec3 pos = vec3(aPosition2, z) * 1. + vec3(0., 0., 0.);

	gl_Position = transpose(cameraData.viewProj) * vec4(pos, 1.0f);

/*
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
*/
	v_color = vec4(1.0);
	//v_color.rgb = v_color.rgb * 1. - clamp(5. * abs(.5 - aUv.x) * abs(.5 - aUv.y), 0., 1.);
	//v_color.rgb = v_color.rgb * 1. - pow(clamp(2. * max(abs(.5 - aUv.x) , abs(.5 - aUv.y)), 0., 1.), 8.0);

	v_uv = aUv;
	v_lvl = pushConstants.levelOffset;
}


