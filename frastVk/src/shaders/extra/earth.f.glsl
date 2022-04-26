#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) in vec2 v_uv;

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 invViewProj;
	vec2 focalLengths;
} cameraData;

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out float outDepth;

/*
        Vector3d ray     = R * Eigen::Map<const Vector3d>{focalPts + i * 3};
        Vector3d v1      = ray.array() * axes_scale;
        double   a_      = v1.dot(v1);
        double   b_      = 2 * v1.dot(p1);
        double   discrim = b_ * b_ - 4 * a_ * c_;
        if (discrim > 0) {
            double   t     = (-b_ - std::sqrt(discrim)) / (2 * a_);
            Vector3d out_  = (ray.array() * axes_scale) * t + eye;
            out[i * 3 + 0] = out_(0);
            out[i * 3 + 1] = out_(1);
            out[i * 3 + 2] = out_(2);
        } else {
            out[i * 3 + 0] = 0;
            out[i * 3 + 1] = 0;
            out[i * 3 + 2] = 0;
        }
		*/

void main() {

	vec2 uv = v_uv;
	/* uv.y = 1.0 - uv.y; */
	/* uv = (uv * 2 - 1.0) * .5; */
	uv = (uv * 2 - 1.0) * cameraData.focalLengths;


	// Ray origin and direction
	/* mat4 ivp = cameraData.invViewProj; */
	mat4 ivp = transpose(cameraData.invViewProj);
	/* vec3 ro = vec3(ivp[0][3], ivp[1][3], ivp[2][3]); */
	vec3 ro = vec3(ivp[3][0], ivp[3][1], ivp[3][2]);
	vec3 rd = (mat3(ivp)) * vec3(uv, 1.0);
	/* vec3 rd = (mat3(ivp)) * vec3(uv, .10); */
	/* vec4 rd_ = ivp * vec4(uv, 0, 1.0); vec3 rd = rd_.xyz / rd_.w; */

	// Intersect with WGS84 ellipsoid.
	/* vec3 axes_scale = vec3(1.0, 1.0, (6378137.0) / 6356752.314245179); */
	vec3 axes_scale = vec3(1.0, 1.0, 1.0);
	vec3 p1 = ro * axes_scale;
	float c = dot(p1,p1) - (1.0 + 0.0);

	vec3 v1 = rd * axes_scale;
	float a = dot(v1,v1);
	float b = 2 * dot(v1, p1);
	float discrim = b*b - 4.*a*c;
	vec3 pt; // final intersection, or 0
	if (discrim > 0) {
		float t = (-b - sqrt(discrim)) / (2.*a);
		pt = (rd * axes_scale) * t + ro;
		if (t < 0.) pt = vec3(0.);
	} else {
		pt = vec3(0.);
	}

	// If hit, shade.
	if (pt[0] != 0. && pt[1] != 0.) {

		/* outFragColor = vec4(0.,0.5,0.5,.75); */
		float l = 0.0;
		float x = atan(pt.x, pt.y) * .5;
		float y = atan(pt.z, length(pt.xy)) * .5;
		/* l += pow(sin(y * 80.), 4.0); */
		/* l += pow(sin(x * 80.), 4.0); */
		l += smoothstep(.1, .0, fract(y*45.));
		l += smoothstep(.1, .0, fract(x*45.));
		l += smoothstep(.1 * .5, .0, fract(y*45.*2.)) * .5;
		l += smoothstep(.1 * .5, .0, fract(x*45.*2.)) * .5;

		vec3 color = vec3(l);
		color += .6;

		color *= .3;
		outFragColor = vec4(color, .75);

		outDepth = length(pt - ro);
	} else {
		outFragColor = vec4(0.,0.,0.,.0);
		outDepth = 1.1;
	}

}
