#version 450

layout (location=0) in vec2 v_uv;
layout (location=1) in vec4 v_color;
layout(set = 0, binding = 1) uniform sampler2D tex;

layout (location = 0) out vec4 outFragColor;

void main()
{

	vec4 final_color;
	final_color = v_color * textureLod(tex, v_uv, 0).rrrr;
	/* final_color += vec4(v_uv / 320., 0.,.1); */

	outFragColor = final_color;
}
