#version 330 core
in vec2 texCoord;
in vec4 tint;
uniform sampler2D fontTexture;
out vec4 fragColor;
void main() {
    fragColor = tint * texture(fontTexture, texCoord);
}
