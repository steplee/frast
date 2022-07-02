#version 450
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) in vec3 a_pos;
layout (location=0) out vec4 v_color;

layout(set=0, binding=0, std430) uniform CameraData {
	mat4 mvp;
} cameraData;


void main()
{
	//output the position of each vertex
	gl_Position = cameraData.mvp * vec4(a_pos, 1.0f);
	v_color = vec4(1.);
}


