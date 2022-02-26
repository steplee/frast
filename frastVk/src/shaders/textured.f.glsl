#version 450

layout (location=0) in vec4 v_color;
layout (location=1) in vec2 v_uv;

layout(set = 1, binding = 0) uniform sampler2D tex;

//output write
layout (location = 0) out vec4 outFragColor;

void main()
{
	//return red
	outFragColor = v_color * texture(tex, v_uv);
}
