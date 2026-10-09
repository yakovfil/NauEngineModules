#version 300 es
precision highp float;
uniform vec4 center;
uniform vec2 extent;
out vec2 uv;
void main()
{
    uv = vec2(float(gl_VertexID & 1), float((gl_VertexID & 2) >> 1));
    gl_Position = center;
    gl_Position.xy += vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) * extent * center.w;
}
