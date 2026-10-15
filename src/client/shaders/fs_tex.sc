$input v_normal, v_color0, v_texcoord0, v_shadowcoord, v_wpos

#include <bgfx_shader.sh>
#include "lighting.sh"

SAMPLER2D(s_color, 0);
SAMPLER2D(s_mask, 1);
uniform vec4 u_params;

void main()
{
	vec4 tex = texture2D(s_color, v_texcoord0);
	if (tex.a < 0.35)
		discard;
	vec4 maskTex = texture2D(s_mask, v_texcoord0);
	float mask = maskTex.r;
	float glow = maskTex.g * u_params.w;
	vec3 tinted = mix(tex.rgb, tex.rgb * v_color0.rgb, u_params.y);
	vec3 albedo = mix(tinted, tex.rgb * v_color0.rgb * 1.5, mask);
	vec3 n = normalize(v_normal);
	float d = dot(n, u_sunDir.xyz);
	float diffuse = mix(abs(d), max(d, 0.0), u_params.z);
	vec3 lit = shade(albedo, n * sign(d + 0.0001), diffuse, v_shadowcoord);
	vec3 color = mix(lit, albedo, u_params.x);
	color = mix(color, albedo * 1.7, glow);
	gl_FragColor = vec4(color, v_color0.a);
}
