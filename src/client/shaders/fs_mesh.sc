$input v_normal, v_color0, v_shadowcoord, v_wpos

#include <bgfx_shader.sh>
#include "lighting.sh"

uniform vec4 u_params;

void main()
{
	vec3 n = normalize(v_normal);
	float diffuse = max(dot(n, u_sunDir.xyz), 0.0);
	vec3 lit = shade(v_color0.rgb, n, diffuse, v_shadowcoord);
	gl_FragColor = vec4(applyFog(mix(lit, v_color0.rgb, u_params.x), v_wpos), v_color0.a);
}
