#version 430 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
uniform mat4 viewProjection;
uniform vec3 aircraftForward;
uniform vec3 aircraftRight;
uniform vec3 aircraftUp;
out vec3 worldNormal;
out vec3 surfaceColor;
void main() {
    vec3 localPosition = aircraftRight * position.x
        + aircraftUp * position.y + aircraftForward * position.z;
    worldNormal = aircraftRight * normal.x + aircraftUp * normal.y + aircraftForward * normal.z;
    surfaceColor = color;
    gl_Position = viewProjection * vec4(localPosition, 1.0);
}
