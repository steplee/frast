#version 450
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) in vec4 v_color;

layout (location = 0) out vec4 outFragColor;

void main()
{
	vec4 color = v_color;
	outFragColor = color;
}
