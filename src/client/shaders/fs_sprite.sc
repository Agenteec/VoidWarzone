$input v_normal, v_color0, v_texcoord0, v_shadowcoord, v_wpos

#include <bgfx_shader.sh>
#include "lighting.sh"

SAMPLER2D(s_color, 0);
uniform vec4 u_params;

void main()
{
	vec4 tex = texture2D(s_color, v_texcoord0);
	float a = tex.a * v_color0.a;
	vec3 rgb = v_color0.rgb * mix(1.0, tex.r, u_params.y);
	if (u_params.x > 0.5)
		gl_FragColor = vec4(rgb * a, a);
	else
		gl_FragColor = vec4(applyFog(rgb, v_wpos), a);
}
