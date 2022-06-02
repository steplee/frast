#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout: enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(location = 0) rayPayloadInEXT vec4 hitValue;
layout(location = 1) rayPayloadEXT vec4 hitValueNext;
hitAttributeEXT vec2 attribs;

struct RtVertex {
	uint pos;
	uint uv;
	uint nrm;
};
layout(std430, set = 0, binding = 3) readonly buffer RtVertexArrays {
	RtVertex vs[];
	/* uint pos; */
	/* uint uv; */
	/* uint nrm; */
	/*uint8_t x,y,z,w;
	uint16_t u,v;
	uint8_t nx,ny,nz;
	uint8_t extra;*/
} vertsArr[800];

layout(std430, set = 0, binding = 4) readonly buffer RtIndices {
	uint inds[];
} indsArr[800];

layout(set = 1, binding = 0) uniform sampler2D texs[800];

layout(std430, set=2, binding=0) uniform CameraData {
	mat4 viewProj;
	vec4 anchor;
	mat4 modelMats[800];
	vec4 uvScaleAndOff[800];
} cameraData;

vec3 unpackPos(uint p) {
	return vec3(
		float((p >> 16) & 0x000000ff),
		float((p >>  8) & 0x000000ff),
		float((p >>  0) & 0x000000ff)).zyx;
}
vec3 unpackNrl(uint p) {
	return vec3(
		float((p >> 16) & 0x000000ff),
		float((p >>  8) & 0x000000ff),
		float((p >>  0) & 0x000000ff)).zyx / 255. - .5;
}
vec2 unpackUv(uint t) {
	return vec2(
		/* float((t & 0xFFFF0000) >> 16), */
		/* float((t & 0x0000FFFF) >> 0 )); */
		/* float((t >> 16) & 0x0000ffff), */
		/* float((t >>  0) & 0x0000ffff)); */
		float((t >>  0) & 0x0000ffff),
		float((t >> 16) & 0x0000ffff));
}

float rand11(float a) { return fract(sin(.5+a*213.123)*47.3); }
vec3 rand13(float a) {
return vec3(
	fract(sin(a*313.123)*77.3),
	fract(sin(22.3+a*113.123)*27.3),
	fract(sin(-12.3-a*213.123)*177.3));
}

void main()
{
  /* const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y); */
  /* hitValue = barycentricCoords * .995; */

  uint objIdx = gl_InstanceCustomIndexEXT;
  /* hitValue = vec3(float(objIdx) / 800., 0., 1.-float(objIdx)/800.); */
  /* hitValue = rand13(float(objIdx)); */


  uint primIdx = gl_PrimitiveID*3;

  uint a,b,c;
  /* uint m1 = 0xffff0000u; uint s1 = 16; */
  /* uint m2 = 0x0000ffffu; uint s2 = 0; */
  uint m2 = 0xffff0000u; uint s2 = 16;
  uint m1 = 0x0000ffffu; uint s1 = 0;
  if (primIdx % 2 == 0) {
	  a = (indsArr[objIdx].inds[primIdx/2] & m1) >> s1;
	  b = (indsArr[objIdx].inds[primIdx/2] & m2) >> s2 ;
	  c = (indsArr[objIdx].inds[primIdx/2+1] & m1) >> s1;
  } else {
	  a = (indsArr[objIdx].inds[primIdx/2] & m2) >> s2;
	  b = (indsArr[objIdx].inds[primIdx/2+1] & m1) >> s1;
	  c = (indsArr[objIdx].inds[primIdx/2+1] & m2) >> s2;
  }

  /* hitValue = vec3(rand11(float(a)), rand11(float(b)), rand11(float(c))); */
  /* if (primIdx == 0) hitValue = vec3(1.); */
  /* return; */

  RtVertex va = vertsArr[objIdx].vs[a];
  RtVertex vb = vertsArr[objIdx].vs[b];
  RtVertex vc = vertsArr[objIdx].vs[c];

  float wa = 1. - attribs.x - attribs.y;
  float wb = attribs.x;
  float wc = attribs.y;

  mat4 model = (cameraData.modelMats[objIdx]);
  vec3 pva = (model * vec4(unpackPos(va.pos), 1.)).xyz;
  vec3 pvb = (model * vec4(unpackPos(vb.pos), 1.)).xyz;
  vec3 pvc = (model * vec4(unpackPos(vc.pos), 1.)).xyz;
  vec3 ctr = wa*pva + wb*pvb + wc*pvc;
  /* hitValue = ctr; */

  /* mat3 R = transpose(inverse(mat3(cameraData.modelMats[objIdx]))); */
  /* mat3 R = inverse(transpose(mat3(cameraData.modelMats[objIdx]))); */
  /* mat3 R = transpose((mat3(cameraData.modelMats[objIdx]))); */
  mat3 R = ((mat3(cameraData.modelMats[objIdx])));
  vec3 na = (R * unpackNrl(va.nrm));
  vec3 nb = (R * unpackNrl(vb.nrm));
  vec3 nc = (R * unpackNrl(vc.nrm));
  vec3 n = normalize(na + nb + nc);

  vec2 uv_scale = cameraData.uvScaleAndOff[objIdx].xy;
  vec2 uv_off = cameraData.uvScaleAndOff[objIdx].zw;
  vec2 uv_a = (unpackUv(va.uv) + uv_off) * uv_scale;
  vec2 uv_b = (unpackUv(vb.uv) + uv_off) * uv_scale;
  vec2 uv_c = (unpackUv(vc.uv) + uv_off) * uv_scale;

  vec2 uv = wa*uv_a + wb*uv_b + wc*uv_c;

  /* hitValue = vec3(uv,0.); */
  vec3 samp = texture(texs[objIdx], uv).rgb;

  vec3 origin = vec3(0.);
  float tmin = .000000001;
  float tmax = 9.0;
  vec3 direction = vec3(0.,0.,1.);

  /*
  if (hitValue.w < 2.) {
      hitValueNext = hitValue;
	  hitValueNext.a += 1.;
	  traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, origin.xyz, tmin, direction.xyz, tmax, 1);
	  hitValue.rgb = samp * (hitValueNext.rgb * .5 + .5);
  } else {
	  hitValue = vec4(normalize(ctr), 1.);
  }
  */
     /* hitValue =  vec4(normalize(ctr), 1.); */
     hitValue =  vec4(normalize(n), 1.);

}
