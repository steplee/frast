#version 450

layout (location=0) in vec4 v_color;
layout (location=1) in vec2 v_uv;
layout (location=2) flat in uint v_tileIndex;

layout(set = 1, binding = 0) uniform sampler2D texs[512];

layout (location = 0) out vec4 outFragColor;

void main()
{
	/* outFragColor = v_color * texture(texs[v_tileIndex], v_uv); */

	vec3 c = texture(texs[v_tileIndex], v_uv).rgb;
	// Here, take v_color to encode axis and angle.
	/* vec3 c = texture(texs[v_tileIndex], v_uv).rgb * 2. - 1.0; */
	/* vec3 axis = v_color.rgb; */
	/* float angle = v_color.a; */
	/*
	mat3 K = mat3(
		0, -axis.z, axis.y,
		axis.z, 0, -axis.x,
		-axis.y, axis.x, 0);
	c += sin(angle) * K*c + (1-cos(angle))*K*K*c;
	*/

	//c += sin(angle) * cross(axis,c) + (1-cos(angle))*cross(axis,cross(axis,c));
	//c = c * .5 + .5;
	outFragColor = vec4(c,1.0);

}

