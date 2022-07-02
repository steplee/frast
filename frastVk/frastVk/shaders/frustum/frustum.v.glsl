#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_scalar_block_layout: enable

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;

layout (location=0) out vec4 v_color;

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
	mat4 model[2];
} cameraData;

void main()
{
	int idx = gl_InstanceIndex;

	vec3 pos = aPos;

	gl_Position = transpose(cameraData.viewProj) * transpose(cameraData.model[idx]) * vec4(pos, 1.0f);

	v_color = aColor;
}
