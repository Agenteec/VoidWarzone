$input v_normal, v_color0, v_texcoord0, v_shadowcoord, v_wpos

#include <bgfx_shader.sh>
#include "lighting.sh"

SAMPLER2D(s_color, 0);
SAMPLER2D(s_mask, 1);
uniform vec4 u_params;
uniform vec4 u_focus[8];

void main()
{
	vec4 tex = texture2D(s_color, v_texcoord0);
	if (tex.a < 0.35)
		discard;
	if (u_params.w > 1.5)
	{
		float hide = 0.0;
		for (int i = 0; i < 8; i++)
		{
			vec4 f = u_focus[i];
			vec3 axis = u_camPos.xyz - f.xyz;
			vec3 rel = v_wpos - f.xyz;
			float t = dot(rel, axis) / max(dot(axis, axis), 0.0001);
			float d = length(rel - axis * clamp(t, 0.0, 1.0));
			float inside = step(0.02, t) * step(f.y + 0.35, v_wpos.y) * step(0.01, f.w);
			hide = max(hide, inside * (1.0 - smoothstep(f.w * 0.75, f.w, d)));
		}
		vec2 cell = mod(floor(gl_FragCoord.xy), 2.0);
		float pattern = cell.x + cell.y * 2.0;
		if (hide > 0.0 && pattern != 0.0)
			discard;
	}
	vec4 maskTex = texture2D(s_mask, v_texcoord0);
	float mask = maskTex.r;
	float glow = maskTex.g * min(u_params.w, 1.0);
	vec3 tinted = mix(tex.rgb, tex.rgb * v_color0.rgb, u_params.y);
	vec3 albedo = mix(tinted, tex.rgb * v_color0.rgb * 1.5, mask);
	vec3 n = normalize(v_normal);
	albedo = snowCover(albedo, n, v_wpos, u_camPos.w * (1.0 - 0.75 * u_params.y) * (1.0 - glow) * (1.0 - u_params.x) * (1.0 - maskTex.b));
	float d = dot(n, u_sunDir.xyz);
	float diffuse = mix(abs(d), max(d, 0.0), u_params.z);
	vec3 lit = shade(albedo, n * sign(d + 0.0001), diffuse, v_shadowcoord);
	vec3 color = mix(lit, albedo, u_params.x);
	color = mix(color, albedo * 1.3, glow);
	gl_FragColor = vec4(applyFow(applyFog(color, v_wpos), v_wpos), v_color0.a);
}
