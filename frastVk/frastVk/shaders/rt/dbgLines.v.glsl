#version 450
#extension GL_EXT_scalar_block_layout: enable

/* layout (location = 0) in vec4 aPosition; */
//layout (location = 1) in vec2 aUv;
//layout (location = 2) in vec3 aNormal;

layout (location=0) out vec4 v_color;
//layout (location=1) out vec2 v_uv;
layout (location=2) flat out uint v_tileIdx;

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	vec4 anchor;
	mat4 modelMats[800];
	vec4 uvScaleAndOff[800];
} cameraData;

layout(push_constant) uniform constants {
	uint tileIndex;
	uint octantMask;
	uint lvl;
} pushConstants;

void main()
{
	vec3 pos = vec3(0.);
	int lineInds[] = int[24](
		0,1, 1,2, 2,3, 3,0,
		4,5, 5,6, 6,7, 7,4,
		0,4, 1,5, 2,6, 3,7);
	int ind = lineInds[gl_VertexIndex];
	//if ((ind%4) == 1 || (ind%4) == 2) pos.x = 255.;
	//if ((ind%4) == 2 || (ind%4) == 3) pos.y = 255.;
	//if (ind >= 4) pos.z = 255.;
	pos = vec3[8](
		vec3(0.,0.,0.),
		vec3(255.,0.,0.),
		vec3(255.,255.,0.),
		vec3(0.,255.,0.),
		vec3(0.,0.,255.),
		vec3(255.,0.,255.),
		vec3(255.,255.,255.),
		vec3(0.,255.,255.))[ind];

	//int octant = int(aPosition.w);
	//float mask = ((pushConstants.octantMask & (1<<octant)) != 0) ? 0. : 1.;
	float mask = 1.0;

	uint tileIndex = pushConstants.tileIndex;

	mat4 modelMat = (cameraData.modelMats[tileIndex]);
	gl_Position = transpose(cameraData.viewProj) * (modelMat * vec4(pos, 1.0f) * mask + cameraData.anchor);

	v_color = vec4(.2, .2, .8, .45);
	v_color.r += float(pushConstants.octantMask) / 5000.;

	v_tileIdx = tileIndex;
}
