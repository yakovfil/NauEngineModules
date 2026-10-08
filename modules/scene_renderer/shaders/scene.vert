#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
uniform mat4 model;
uniform mat4 viewProjection;
uniform mat4 normalTransform;
out vec3 worldNormal;
void main()
{
    gl_Position = viewProjection * model * vec4(position, 1.0);
    worldNormal = (normalTransform * vec4(normal, 0.0)).xyz;
}
