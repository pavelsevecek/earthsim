#version 430 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
uniform mat4 viewProjection;
uniform mat4 lightViewProjection[2];
out vec3 worldPosition;
out vec3 worldNormal;
out vec4 shadowPosition[2];
void main() {
    worldPosition = position;
    worldNormal = normal;
    for(int i = 0; i < 2; ++i) shadowPosition[i] = lightViewProjection[i] * vec4(position, 1.0);
    gl_Position = viewProjection * vec4(position, 1.0);
}
