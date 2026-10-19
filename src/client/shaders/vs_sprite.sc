$input a_position, a_normal, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_normal, v_color0, v_texcoord0, v_shadowcoord, v_wpos

#include <bgfx_shader.sh>

void main()
{
	mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));
	vec4 worldPos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, worldPos);
	v_normal = vec3(0.0, 1.0, 0.0);
	v_color0 = i_data4;
	v_texcoord0 = vec2(i_data0.w, i_data1.w) + a_texcoord0 * i_data2.w;
	v_wpos = worldPos.xyz;
	v_shadowcoord = vec4(0.0, 0.0, 0.0, 1.0);
}
