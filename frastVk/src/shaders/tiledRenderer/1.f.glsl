#version 450

layout (location=0) in vec4 v_color;
layout (location=1) in vec2 v_uv;
//layout (location=2) in flat int v_lvl;
layout (location=2) in flat uint v_tileId;

layout(set = 1, binding = 0) uniform sampler2D tex[128];

//output write
layout (location = 0) out vec4 outFragColor;

void main()
{
	//return red


	vec4 color = v_color;


	vec4 final_color = color * texture(tex[v_tileId], v_uv);

	//color.rgb = color.rgb * (1. - pow(clamp(2. * max(abs(.5 - v_uv.x) , abs(.5 - v_uv.y)), 0., 1.), 4.0));
	if (abs(.5 - v_uv.x) > .495) final_color.b = (final_color.b+1.)*.5;
	if (abs(.5 - v_uv.y) > .495) final_color.b = (final_color.b+1.)*.5;

	outFragColor = final_color;
}
