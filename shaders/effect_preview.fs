#version 330 core
out vec4 FragColor;

uniform vec3 color;
uniform sampler2D effectTex;
uniform bool useTexture;
uniform float alphaStart;
uniform float alphaEnd;

in float vLife;

void main()
{
	vec2 uv01 = vec2(gl_PointCoord.x, 1.0 - gl_PointCoord.y);
	vec2 uv = uv01 * 2.0 - 1.0;
	float radial = dot(uv, uv);
	if (radial > 1.0)
		discard;

	vec4 tex = useTexture ? texture(effectTex, uv01) : vec4(1.0);
	vec4 texBlur = vec4(0.0);
	if (useTexture)
	{
		// A tiny blur kernel turns sparse sprite pixels into readable glows.
		vec2 t = vec2(0.04, 0.04);
		texBlur += texture(effectTex, uv01 + vec2(-t.x, 0.0));
		texBlur += texture(effectTex, uv01 + vec2( t.x, 0.0));
		texBlur += texture(effectTex, uv01 + vec2(0.0, -t.y));
		texBlur += texture(effectTex, uv01 + vec2(0.0,  t.y));
		texBlur *= 0.25;
	}
	float lifeAlpha = mix(alphaEnd, alphaStart, clamp(vLife, 0.0, 1.0));
	float radialSoft = smoothstep(1.0, 0.0, radial);
	float textureAlpha = max(tex.a, texBlur.a * 0.85);
	float shapeAlpha = useTexture ? (textureAlpha * lifeAlpha * radialSoft * 2.8) : ((1.0 - radial) * lifeAlpha);
	shapeAlpha = clamp(shapeAlpha, 0.0, 1.0);
	if (shapeAlpha < 0.01)
		discard;

	vec3 rgb = useTexture ? max(tex.rgb, texBlur.rgb * 0.7) : color;
	FragColor = vec4(rgb * color, shapeAlpha);
}
