uniform vec4 u_sunDir;
uniform vec4 u_sunColor;
uniform vec4 u_skyColor;
uniform vec4 u_shadowParams;
SAMPLER2DSHADOW(s_shadow, 4);

float shadowFactor(vec4 coord)
{
	vec3 c = coord.xyz / coord.w;
	if (u_sunDir.w < 0.5 || c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0)
		return 1.0;
	float t = u_shadowParams.x;
	float z = c.z - u_shadowParams.y;
	float s = shadow2D(s_shadow, vec3(c.xy, z)) * 2.0;
	s += shadow2D(s_shadow, vec3(c.xy + vec2(-t, -t), z));
	s += shadow2D(s_shadow, vec3(c.xy + vec2( t, -t), z));
	s += shadow2D(s_shadow, vec3(c.xy + vec2(-t,  t), z));
	s += shadow2D(s_shadow, vec3(c.xy + vec2( t,  t), z));
	return s / 6.0;
}

vec3 shade(vec3 albedo, vec3 n, float diffuse, vec4 shadowCoord)
{
	float sh = shadowFactor(shadowCoord);
	vec3 ambient = u_skyColor.rgb * (0.75 + 0.25 * n.y);
	return albedo * (ambient + u_sunColor.rgb * diffuse * sh);
}
