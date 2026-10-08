#version 300 es
precision highp float;
in vec3 worldNormal;
uniform vec3 baseColor;
uniform vec3 lightDirection;
uniform vec3 ambient;
uniform vec3 directional;
out vec4 color;
void main()
{
    vec3 linear = clamp(baseColor * (ambient + directional * max(dot(normalize(worldNormal), lightDirection), 0.0)), 0.0, 1.0);
    vec3 display = mix(linear * 12.92, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), linear));
    color = vec4(display, 1.0);
}
