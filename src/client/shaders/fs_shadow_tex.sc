$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);

void main()
{
	if (texture2D(s_color, v_texcoord0).a < 0.35)
		discard;
	gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
