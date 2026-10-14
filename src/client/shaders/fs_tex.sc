$input v_normal, v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);
SAMPLER2D(s_mask, 1);
uniform vec4 u_params;

void main()
{
	vec4 tex = texture2D(s_color, v_texcoord0);
	if (tex.a < 0.35)
		discard;
	float mask = texture2D(s_mask, v_texcoord0).r;
	vec3 tinted = mix(tex.rgb, tex.rgb * v_color0.rgb, u_params.y);
	vec3 albedo = mix(tinted, tex.rgb * v_color0.rgb * 1.5, mask);
	vec3 light = normalize(vec3(-0.4, 1.0, -0.6));
	float diffuse = abs(dot(normalize(v_normal), light));
	vec3 lit = albedo * (0.45 + 0.65 * diffuse);
	gl_FragColor = vec4(mix(lit, albedo, u_params.x), v_color0.a);
}
