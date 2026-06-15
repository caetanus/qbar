#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    vec4 baseColor;
    vec4 highlightColor;
    vec4 shadowColor;
    float edgeSize;
    float cornerRadius;
    float padding;
    float intensity;
};

float roundedMask(vec2 p, float radius)
{
    vec2 q = abs(p - vec2(0.5)) - vec2(0.5 - radius);
    float outside = length(max(q, vec2(0.0)));
    float inside = min(max(q.x, q.y), 0.0);
    return 1.0 - smoothstep(0.0, 0.004, outside + inside);
}

void main()
{
    vec2 p = qt_TexCoord0;
    float alpha = roundedMask(p, clamp(cornerRadius, 0.0, 0.5));
    if (alpha <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    float edge = max(edgeSize, 0.002);
    float top = 1.0 - smoothstep(0.0, edge * 1.75, p.y);
    float left = 1.0 - smoothstep(0.0, edge * 1.75, p.x);
    float bottom = 1.0 - smoothstep(1.0 - edge * 2.50, 1.0, p.y);
    float right = 1.0 - smoothstep(1.0 - edge * 2.50, 1.0, p.x);

    float cornerGlow = (1.0 - smoothstep(0.0, edge * 1.25, p.x)) * (1.0 - smoothstep(0.0, edge * 1.25, p.y));
    float cornerShade = (1.0 - smoothstep(1.0 - edge * 1.25, 1.0, p.x)) * (1.0 - smoothstep(1.0 - edge * 1.25, 1.0, p.y));

    float light = clamp((top + left + cornerGlow) / 3.0, 0.0, 1.0);
    float dark = clamp((bottom + right + cornerShade) / 3.0, 0.0, 1.0);
    vec3 color = baseColor.rgb;
    color = mix(color, highlightColor.rgb, light * intensity);
    color = mix(color, shadowColor.rgb, dark * intensity);
    color = mix(color, baseColor.rgb * 1.10, clamp(1.0 - abs(p.y - 0.25) * 3.0, 0.0, 1.0) * 0.08);

    fragColor = vec4(color, baseColor.a * alpha);
}
