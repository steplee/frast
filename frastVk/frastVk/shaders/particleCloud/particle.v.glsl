#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) in vec3 aPosition;
layout (location=1) in float intensity;

layout (location=0) out vec4 v_color;

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 viewProj;
} cameraData;


void main()
{
	gl_PointSize = 1;
	gl_Position = transpose(cameraData.viewProj) * vec4(aPosition,1.0);
	/* gl_Position = transpose(cameraData.viewProj) * vec4(0.153356, -0.766782 , 0.644097, 1.); */
	/* v_color = vec4(vec3(intensity), 1.0); */
	/* v_color = vec4(1.0, 1.0 ,1.0, intensity); */
	/* v_color = vec4(1.); */

	/* v_color = vec4(intensity); */
	v_color = vec4(
		intensity,
		pow(intensity,8.0),
		mix(pow(intensity,2.0), intensity, .5),
		clamp(pow(intensity,.5), .001, .2)
	);

}
