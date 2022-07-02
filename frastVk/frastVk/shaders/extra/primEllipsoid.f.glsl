#version 450
precision highp float;
#extension GL_EXT_shader_16bit_storage: enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16: enable
#extension GL_EXT_scalar_block_layout: enable

layout (location=0) in vec2 v_uv;
layout (location=1) in flat uint v_instance;

struct EllipsoidData {
	mat4 matrix;
	vec4 color;
};

layout(std430, set=0, binding=0) uniform CameraData {
	mat4 invViewProj;
	EllipsoidData datas[4];
} cameraData;

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out float outFragDepth;

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
	uv = (uv * 2 - 1.0);


	// Ray origin and direction
	mat4 ivp = transpose(cameraData.invViewProj);
	vec3 ro = vec3(ivp[3][0], ivp[3][1], ivp[3][2]);
	vec3 rd = (mat3(ivp)) * vec3(uv, 1.0);

	// Intersect with WGS84 ellipsoid.
	/*
		vec3 axes_scale = vec3(1.0, 1.0, (6378137.0) / 6356752.314245179);
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
	*/

	uint index = v_instance;
	vec4 p1_ = transpose(cameraData.datas[index].matrix) * vec4(ro,1.0);
	vec3 p1 = p1_.xyz / p1_.w;
	/* vec4 v1_ = transpose(cameraData.datas[index].matrix) * vec4(rd,1.0); */
	/* vec3 v1 = transpose(inverse(mat3(cameraData.datas[index].matrix))) * rd; */
	/* vec3 v1 = transpose(mat3(cameraData.datas[index].matrix)) * rd; */
	vec3 v1 = transpose(mat3(cameraData.datas[index].matrix)) * rd;
	/* vec3 v1 = transpose(inverse(transpose(mat3(cameraData.datas[index].matrix)))) * rd; */

	float a = dot(v1,v1);
	float b = 2 * dot(v1, p1);
	float c = dot(p1,p1) - 1.0;
	float discrim = b*b - 4.*a*c;
	vec3 pt; // final intersection, or 0
	float t = -1.;
	if (discrim > 0) {
		float t1 = (-b - sqrt(discrim)) / (2.*a);
		float t2 = (-b + sqrt(discrim)) / (2.*a);
		if (t1 > 0 && (t1 < t2 || t2 <= 0.)) t = t1;
		else t = t2;
		pt = (v1) * t + ro;
		if (t < 0.) pt = vec3(0.);
	} else {
		pt = vec3(0.);
	}
	
	vec4 outColor = vec4(0.);
	float outDepth;

	// If hit, shade.
	if (pt[0] != 0. && pt[1] != 0.) {

		float d = length(ro-pt);

		d = d > 5.0 ? (d*d) : d;
		float o = exp(-(d)*13.5-1.30) * 25.;

		float l = 0.0;
		float x = atan(pt.y, pt.x) * 2. / 3.14159265359;
		float y = atan(pt.z, length(pt.xy)) * 2. / 3.14159265359;

		outColor = cameraData.datas[index].color;
		vec3 ctr = vec3(cameraData.datas[index].matrix[3][0],
						cameraData.datas[index].matrix[3][1],
						cameraData.datas[index].matrix[3][2]);
		vec3 nrml = normalize(pt-ctr);
		/* outColor *= clamp(dot(normalize(v1),nrml),0.,1.); */
		outColor.a *= pow(dot(normalize(rd),nrml),4.);
		/* outColor *= clamp(dot(vec3(0.,0.,1.),nrml),0.,1.); */
		/* outColor = vec4(1.,1.,1.,.5); */

		// THIS IS WRONG: I think you need actual projection matrix...
		/* vec4 pt4 = transpose(cameraData.datas[index].matrix) * vec4(ro,1.0); */
		/* vec3 pt5 = pt4.xyz / pt4.w; */
		/* outDepth = length(pt5 - ro); */
		outDepth = .05;
	} else {
		outDepth = 1.1;
	}

	outFragColor = outColor;
	/* outFragDepth = .0; */
	gl_FragDepth = outDepth;

}

