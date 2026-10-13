$input v_normal, v_color0

#include <bgfx_shader.sh>

uniform vec4 u_params;

void main()
{
	vec3 light = normalize(vec3(-0.4, 1.0, -0.6));
	float diffuse = max(dot(normalize(v_normal), light), 0.0);
	vec3 lit = v_color0.rgb * (0.35 + 0.65 * diffuse);
	gl_FragColor = vec4(mix(lit, v_color0.rgb, u_params.x), v_color0.a);
}
