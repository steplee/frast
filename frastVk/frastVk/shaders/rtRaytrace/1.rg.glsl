#version 460
#extension GL_EXT_ray_tracing: enable
#extension GL_EXT_scalar_block_layout: enable

##include "1.common.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(std430, binding = 2, set = 0) uniform CameraProperties
{
	mat4 viewInverse;
	mat4 projInverse;
} cam;
layout(set = 1, binding = 0) uniform sampler2D texs[800];


/* layout(location = 0) rayPayloadEXT vec4 hitValue; */
layout(location = 0) rayPayloadEXT RayType payload;

void main()
{
	const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
	const vec2 inUV = pixelCenter/vec2(gl_LaunchSizeEXT.xy);
	vec2 d = inUV * 2.0 - 1.0;

	vec4 origin = cam.viewInverse * vec4(0,0,0,1);
	/* vec4 target = cam.projInverse * vec4(d.x, d.y, 1, 1) ; */
	vec4 target = cam.projInverse * vec4(d.x, d.y, 1, 1) ;
	/* vec4 target = vec4(d.x, d.y, 3., 3.) ; */

	/* vec4 direction = cam.viewInverse*vec4(normalize(target.xyz), 0) ; */
	vec3 direction = mat3(cam.viewInverse)*normalize(target.xyz);

	/* float tmin = 10. / (6378137.0); */
	float tmin = .000000001;
	float tmax = 9.0;

    /* payload.ro = origin; */
    /* payload.rd = direction; */
    payload.color = vec4(1.);
	payload.depth = 0;

    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, origin.xyz, tmin, direction.xyz, tmax, 0);
	/* imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue * .5 + .2*direction.xyz, 0.0)); */
	imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(payload.color.rgb, 1.0));
}
