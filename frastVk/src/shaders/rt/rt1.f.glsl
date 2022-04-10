#version 450

layout (location=0) in vec4 v_color;
layout (location=1) in vec2 v_uv;
layout (location=2) flat in uint v_tileIndex;

layout(set = 1, binding = 0) uniform sampler2D texs[256];

layout (location = 0) out vec4 outFragColor;

void main()
{
	outFragColor = v_color * texture(texs[v_tileIndex], v_uv);
}

