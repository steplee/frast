#version 450

layout (location = 0) out vec4 outFragColor;

layout(std430, push_constant) uniform PushConstants {
	vec4 color;
} pushConstants;

void main()
{
	outFragColor = pushConstants.color;
	/* outFragColor = vec4(1.); */
}
