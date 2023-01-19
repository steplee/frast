#pragma once

#include <string>

namespace {

const std::string earth_ellps_vsrc = R"(#version 440

//in layout(location=0) vec4 a_pos;
//in layout(location=1) vec2 a_uv;

out vec2 v_uv;

void main() {
	//v_uv = a_uv;

	//gl_Position = u_mvp * vec4(a_pos.xyz, 1.0);
	// gl_Position = vec4(.0,.0,.0,1.);
	
	vec3 a_pos[6] = vec3[](
		vec3(0., 0., 0.),
		vec3(1., 0., 0.),
		vec3(1., 1., 0.),

		vec3(1., 1., 0.),
		vec3(0., 1., 0.),
		vec3(0., 0., 0.));

	// gl_Position = vec4(a_pos[gl_VertexID], 1.);
	gl_Position = vec4(a_pos[gl_VertexID] * 2. - 1., 1.);
	v_uv = a_pos[gl_VertexID].xy;

}

)";


const std::string earth_ellps_fsrc = R"(#version 440

uniform layout(location=0) mat4 u_invMvp;

in vec2 v_uv;

out layout(location=0) vec4 out_color;
// out layout(location=1) float out_frag_depth;

/*
void main() {
	out_color = vec4(.5);
}
*/

void main() {

	vec2 uv = v_uv;
	uv = (uv * 2 - 1.0);
	uv.y = -uv.y;


	// Ray origin and direction
	mat4 ivp = (u_invMvp);
	vec3 ro = vec3(ivp[3][0], ivp[3][1], ivp[3][2]);
	vec3 rd = (mat3(ivp)) * vec3(uv, 1.0);
	// rd = -(rd);
	// rd = normalize(rd);

	// Intersect with WGS84 ellipsoid.
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

	float outDepth;
	vec4 outColor;

	// If hit, shade.
	if (pt[0] != 0. && pt[1] != 0.) {

		float d = length(ro) - .997;

		d = d > 5.0 ? (d*d) : d;
		float o = exp(-(d)*13.5-1.30) * 25.;

		float l = 0.0;
		float x = atan(pt.y, pt.x) * 2. / 3.14159265359;
		float y = atan(pt.z, length(pt.xy)) * 2. / 3.14159265359;
		/* l += pow(sin(y * 80.), 4.0); */
		/* l += pow(sin(x * 80.), 4.0); */


		float f = 8.0;
		for (int i=0; i<4; i++) {
			float oo = o + i * .5;
			float io = floor(oo);
			float mo = 1. - fract(oo);
			float io1 = pow(2.0, io);
			float io2 = pow(2.0, io+1.0);
			/* l += mo*smoothstep(.01*mo, .0, abs(fract(f*y*io1)-.5)); */
			/* l += mo*smoothstep(.01*mo, .0, abs(fract(f*x*io1)-.5)); */
			/* l += (1.-mo)*smoothstep(.01*(1.-mo), .0, abs(fract(f*y*io2)-.5)); */
			/* l += (1.-mo)*smoothstep(.01*(1.-mo), .0, abs(fract(f*x*io2)-.5)); */
			l = max(l,mo*smoothstep(.01*mo, .0, abs(fract(f*x*io1)-.5)));
			l = max(l,(1.-mo)*smoothstep(.01*(1.-mo), .0, abs(fract(f*x*io2)-.5)));
			l = max(l,mo*smoothstep(.01*mo, .0, abs(fract(f*y*io1)-.5)));
			l = max(l,(1.-mo)*smoothstep(.01*(1.-mo), .0, abs(fract(f*y*io2)-.5)));
		}

		l *= .5;
		vec3 color = vec3(l);
		color += .2;

		color *= .5;
		outColor = vec4(color, 1.*length(color.rgb));

		/* outDepth = length(pt - ro); */
		outDepth = .99999;
	} else {
		outColor = vec4(0.,0.,0.,.0);
		outDepth = 1.1;
	}

	out_color = outColor;
	gl_FragDepth = outDepth;

}


)";




}

