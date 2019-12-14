#version 150
// please send comments or report bug to: liangliang.nan@gmail.com

uniform bool	perspective;

uniform mat4	PROJ;
uniform float	sphere_radius;

uniform vec4 modelColor;

in  vec4 position; // in eye space

out vec4 outputF;


void main()
{
	/*  with perspective correction
	*   Ref: Learning Modern 3D Graphics Programming, by Jason L. McKesson
	*	http://alfonse.bitbucket.org/oldtut/Illumination/Tut13%20Correct%20Chicanery.html
	**/
	if (perspective) {
		vec2 tex = gl_PointCoord* 2.0 - vec2(1.0);
		tex = vec2(tex.x, -tex.y) * 1.5; // 1.5 times larger ensure the quad is big enought in perspective view

		vec3 planePos = vec3(tex * sphere_radius, 0.0) + position.xyz;
		vec3 view_dir = normalize(planePos);
		float B = 2.0 * dot(view_dir, -position.xyz);
		float C = dot(position.xyz, position.xyz) - (sphere_radius * sphere_radius);
		float det = (B * B) - (4 * C);
		if (det < 0.0)
			discard;

		float sqrtDet = sqrt(det);
		float posT = (-B + sqrtDet) / 2;
		float negT = (-B - sqrtDet) / 2;
		float intersectT = min(posT, negT);
		vec3 pos = view_dir * intersectT;

		// compute the depth. I can do it easier
		//vec4 pos4 = PROJ * vec4(pos, 1.0);
		//gl_FragDepth = 0.5*(pos4.z / pos4.w) + 0.5;
		vec2 clipZW = pos.z * PROJ[2].zw + PROJ[3].zw;
		gl_FragDepth = 0.5 * clipZW.x / clipZW.y + 0.5;

		outputF = modelColor;
	}

	// without perspective correction
	else {
		// r^2 = (x - x0)^2 + (y - y0)^2 + (z - z0)^2
		vec2 tex = gl_PointCoord* 2.0 - vec2(1.0);
		float x = tex.x;
		float y = -tex.y;
		float zz = 1.0 - x*x - y*y;

		if (zz < 0.0)
			discard;

		float z = sqrt(zz);
		vec4 pos = position;
		pos.z += sphere_radius*z;

		// compute the depth. I can do it easier
		//pos = PROJ * pos;
		//gl_FragDepth = 0.5*(pos.z / pos.w) + 0.5;
		vec2 clipZW = pos.z * PROJ[2].zw + PROJ[3].zw;
		gl_FragDepth = 0.5 * clipZW.x / clipZW.y + 0.5;

		outputF = modelColor;
	}
}
