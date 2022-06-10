#version 460
#extension GL_EXT_ray_tracing : enable

##include "1.common.glsl"

layout(location = 0) rayPayloadInEXT RayType payload;

void main()
{
    /* payload.color = vec4(vec3(.95), 1.); */
	vec3 rd = normalize(gl_WorldRayDirectionEXT);

	vec3 sunPos = normalize(vec3(.1,-1,.5));

	float i = smoothstep(.8,.9, abs(dot(rd,sunPos)));
	i = i * .7 + .3;
    payload.color = vec4(i * vec3(.95), 1.);
	payload.depth += 1u;
}
