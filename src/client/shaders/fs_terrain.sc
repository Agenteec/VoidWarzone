$input v_normal, v_color0, v_shadowcoord, v_wpos

#include <bgfx_shader.sh>
#include "lighting.sh"

SAMPLER2D(s_ground, 0);
SAMPLER2D(s_dirt, 1);
SAMPLER2D(s_cliff, 2);
SAMPLER2D(s_road, 3);

void main()
{
	vec3 n = normalize(v_normal);
	vec2 top = v_wpos.xz;
	vec2 side = abs(n.x) > abs(n.z) ? vec2(v_wpos.z, v_wpos.y) : vec2(v_wpos.x, v_wpos.y);
	vec3 ground = mix(texture2D(s_ground, top / 8.0).rgb, texture2D(s_ground, top / 21.7 + vec2(0.37, 0.61)).rgb, 0.5);
	vec3 dirt = texture2D(s_dirt, top / 6.0).rgb;
	vec3 cliff = mix(texture2D(s_cliff, side / 3.0).rgb, texture2D(s_cliff, top / 4.0).rgb, clamp(n.y * 1.6 - 0.6, 0.0, 1.0));
	vec3 road = texture2D(s_road, top / 4.0).rgb;
	vec4 w = v_color0 / max(dot(v_color0, vec4_splat(1.0)), 0.001);
	vec3 albedo = ground * w.x + dirt * w.y + cliff * w.z + road * w.w;
	float diffuse = max(dot(n, u_sunDir.xyz), 0.0);
	gl_FragColor = vec4(applyFow(applyFog(shade(albedo, n, diffuse, v_shadowcoord), v_wpos), v_wpos), 1.0);
}
