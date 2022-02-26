#version 450

layout (location=0) in vec4 v_color;
layout (location=1) in vec2 v_uv;
layout (location=2) in flat int v_lvl;

layout(set = 1, binding = 0) uniform sampler2D tex[4];

//output write
layout (location = 0) out vec4 outFragColor;

void main()
{
	//return red
	outFragColor = v_color * texture(tex[v_lvl], v_uv);

	//outFragColor = v_color;
}
