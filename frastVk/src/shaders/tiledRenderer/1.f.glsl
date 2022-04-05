#version 450

layout (location=0) in vec4 v_color;
layout (location=1) in vec2 v_uv;
//layout (location=2) in flat int v_lvl;
layout (location=2) in flat uint v_tileId;

layout(set = 1, binding = 0) uniform sampler2D tex[128];

//output write
layout (location = 0) out vec4 outFragColor;

layout (push_constant) uniform PushConstants {
	bool grayscale;
} pushConstants;

void main()
{
	//return red


	vec4 color = v_color;


	vec4 final_color;
	if (pushConstants.grayscale) final_color = color * texture(tex[v_tileId], v_uv).rrra;
	else final_color = color * texture(tex[v_tileId], v_uv);

	//if (abs(.5 - v_uv.x) > .492 || abs(.5 - v_uv.y) > .492) final_color.b = 1.0;

	outFragColor = final_color;
}
