#version 300 es
precision highp float;
uniform sampler2D marker;
in vec2 uv;
out vec4 color;
void main()
{
    vec4 sampleColor = texture(marker, uv);
    if (sampleColor.a < 0.01) discard;
    vec3 display = mix(sampleColor.rgb * 12.92, 1.055 * pow(sampleColor.rgb, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), sampleColor.rgb));
    color = vec4(display, 1.0);
}
