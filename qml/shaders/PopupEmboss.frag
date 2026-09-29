#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// ShaderEffect shares one uniform buffer with its (default) vertex shader:
// qt_Matrix must sit at offset 0 and qt_Opacity at 64, even when unused here.
// Without them, baseColor aliased the matrix and the card body came out
// fully transparent.
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 baseColor;
    vec4 highlightColor;
    vec4 shadowColor;
    float edgeSize;
    float cornerRadius;
    float padding;
    float intensity;
    vec2 itemSize;   // px; the mask is computed in pixels, not UV
};

// Rounded-rectangle coverage in pixels (1px antialiased edge). `radius` is
// normalised to the item's shorter side, as the QML side passes it. In UV
// space a non-square card would get elliptical corners, and dropping the
// SDF's `- r` term shrinks the whole box by r on every side.
float roundedMask(vec2 p, float radius)
{
    vec2 size = max(itemSize, vec2(1.0));
    float r = radius * min(size.x, size.y);
    vec2 halfSize = size * 0.5;
    vec2 q = abs(p * size - halfSize) - (halfSize - vec2(r));
    float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
    return 1.0 - smoothstep(-0.75, 0.75, d);
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

    // Qt Quick blends premultiplied: scale rgb by the final alpha, and honour
    // the item opacity (the card's fade in/out).
    float a = baseColor.a * alpha * qt_Opacity;
    fragColor = vec4(color * a, a);
}
