#version 440

// Cover Flow card: the item's quad shows a textured card rotated around its vertical
// centre axis with true perspective. `rest` (<1) shrinks the resting card inside the
// item, leaving headroom so the near vertical edge can grow TALLER than the far one
// (the actual 3D-turn cue) without clipping — instead of a symmetric horizontal squash.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float angle;   // radians; 0 = facing the viewer
    float depth;   // perspective distance (~1.0-1.6); smaller = stronger 3D
    float rest;    // resting card size as a fraction of the item (~0.72)
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    // item-space, centred, expanded by 1/rest so the face occupies [-0.5,0.5] at rest.
    vec2 p = (qt_TexCoord0 - vec2(0.5)) / max(rest, 0.05);

    float c = cos(angle);
    float s = sin(angle);

    // inverse-perspective map: screen x -> local face x in [-0.5,0.5]
    float u = p.x / (c + p.x * s / depth);
    // depth of that column, and the perspective magnification there
    float z = u * s;
    float vscale = 1.0 / (1.0 - z / depth);
    // face y, de-magnified so the near column (vscale>1) fills more of the item (taller)
    float fy = p.y / vscale;

    if (abs(u) > 0.5 || abs(fy) > 0.5) {
        fragColor = vec4(0.0);
        return;
    }

    vec2 uv = vec2(u + 0.5, fy + 0.5);
    vec4 col = texture(source, uv);

    // shade the turned-away (far, foreshortened) side darker so the turn reads as 3D.
    float shade = mix(0.42, 1.0, smoothstep(0.62, 1.35, vscale));
    col.rgb *= shade;

    fragColor = col * qt_Opacity;
}
