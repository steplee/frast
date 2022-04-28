#version 450

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUv;
//layout (location = 2) in vec3 aNormal;

layout (location=0) out vec4 v_color;
layout (location=1) out vec2 v_uv;

layout(set=0, binding=0) uniform CameraData {
	mat4 viewProj;
} cameraData;

layout(push_constant) uniform constants {
	mat4 modelMat;
} PushConstants;

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
	vec3 pos = aPosition;

	//output the position of each vertex
	//gl_Position = cameraData.viewProj * PushConstants.modelMat * vec4(aPosition, 1.0f);
	gl_Position = transpose(cameraData.viewProj) * transpose(PushConstants.modelMat) * vec4(pos, 1.0f);

	//const array of positions for the triangle
	const vec3 positions[3] = vec3[3](
		vec3(1.f,1.f, 0.0f),
		vec3(-1.f,1.f, 0.0f),
		vec3(0.f,-1.f, 0.0f)
	);
	//output the position of each vertex
	gl_Position = transpose(cameraData.viewProj) * vec4(positions[gl_VertexIndex], 1.0f);


	vec4 c = vec4(aUv.xxy, 1.0f);
	//vec4 c = vec4(1.0f);
	v_color = c;
	v_uv = aUv;
}

