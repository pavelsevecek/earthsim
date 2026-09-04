#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
uniform vec2 displayPosition;
uniform vec2 displaySize;
out vec2 texCoord;
out vec4 tint;
void main() {
    vec2 p = (position - displayPosition) / displaySize;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    texCoord = uv;
    tint = color;
}
