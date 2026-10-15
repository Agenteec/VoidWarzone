$input v_normal, v_color0, v_texcoord0, v_shadowcoord, v_wpos

#include <bgfx_shader.sh>

uniform vec4 u_time;

float hash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

void main()
{
	vec2 p = v_texcoord0 * 2.0 - 1.0;
	float r = length(p);
	if (r > 1.0)
		discard;
	float t = u_time.x;
	float a = atan2(p.y, p.x);
	float swirl = a * 3.0 + r * 10.0 - t * 2.4 + noise(p * 3.0 + t * 0.3) * 1.5;
	float arms = pow(sin(swirl) * 0.5 + 0.5, 2.0);
	float fine = noise(vec2(a * 5.0 + t * 0.8, r * 7.0 - t * 2.0));
	float vortex = arms * 0.7 + fine * 0.3;
	vec3 glow = v_color0.rgb;
	vec3 hot = mix(glow, vec3(0.55, 0.85, 1.0), 0.35);
	vec3 deep = glow * 0.05;
	float depth = smoothstep(1.0, 0.0, r);
	vec3 color = mix(deep, hot * 1.5, vortex * (0.25 + 0.75 * (1.0 - depth * 0.6)));
	color += vec3(1.0, 0.85, 1.0) * pow(max(depth - 0.5, 0.0) * 2.0, 3.0) * (0.8 + 0.4 * sin(t * 3.0));
	float rim = smoothstep(0.72, 0.96, r) * (1.0 - smoothstep(0.96, 1.0, r));
	color += glow * rim * (2.2 + 0.8 * sin(a * 6.0 - t * 4.0));
	float sparks = step(0.97, noise(vec2(a * 20.0, r * 30.0 - t * 6.0)));
	color += vec3(1.0, 0.9, 1.0) * sparks * (1.0 - depth * 0.5);
	float alpha = (1.0 - smoothstep(0.95, 1.0, r)) * 0.97;
	gl_FragColor = vec4(color, alpha);
}
