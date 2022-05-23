#version 450
#extension GL_EXT_scalar_block_layout: enable

layout (location = 0) in vec4 aPosition;
layout (location = 1) in vec2 aUv;
layout (location = 2) in vec3 aNormal;

layout (location=0) out vec4 v_color;
layout (location=1) out vec2 v_uv;
layout (location=2) flat out uint v_tileIdx;
layout (location=3) out vec4 v_caster_uv; // uv1, uv2

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	vec4 anchor;
	mat4 modelMats[800];
	vec4 uvScaleAndOff[800];
} cameraData;

layout(std430, set=2, binding=0) uniform CasterData {
	mat4 casterMatrix[2];
	vec4 color1;
	vec4 color2;
	uint casterMask;
} casterData;

layout(push_constant) uniform constants {
	/* mat4 modelMat; */
	uint tileIndex;
	uint octantMask;
	uint lvl;
} pushConstants;


void main()
{

	vec3 pos1 = aPosition.xyz;

	int octant = int(aPosition.w);
	float mask = ((pushConstants.octantMask & (1<<octant)) != 0) ? 0. : 1.;


	uint tileIndex = pushConstants.tileIndex;

	mat4 modelMat = (cameraData.modelMats[tileIndex]);
	vec4 pos2 = modelMat * vec4(pos1, 1.0f) * mask;
	gl_Position = transpose(cameraData.viewProj) * (pos2 + cameraData.anchor);

	vec2 uv_scale = cameraData.uvScaleAndOff[tileIndex].xy;
	vec2 uv_off = cameraData.uvScaleAndOff[tileIndex].zw;
	v_uv = (aUv + uv_off) * uv_scale * mask;

	vec4 c = vec4(1.0f);
	/* float flvl = float(pushConstants.lvl); */
	/* flvl += pos.x / 255.0; */
	v_color = c;

	v_tileIdx = tileIndex;


	//
	// Caster stuff
	//
	// Project the vertex into the caster frustum.
	// If it lies outside, make the uv = <0,0>
	if ((casterData.casterMask & 1) != 0u) {
		vec4 pp = transpose(casterData.casterMatrix[0]) * pos2;
		vec3 p = pp.xyz / pp.w;
		v_caster_uv.xy = (p.xy) * .5 + .5;
	} else v_caster_uv.xy = vec2(0.);

	if ((casterData.casterMask & 2) != 0u) {
		vec4 pp = transpose(casterData.casterMatrix[1]) * pos2;
		vec3 p = pp.xyz / pp.w;
		v_caster_uv.zw = (p.xy) * .5 + .5;
	} else v_caster_uv.zw = vec2(0.);

}



