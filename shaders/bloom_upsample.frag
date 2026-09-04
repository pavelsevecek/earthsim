#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D source;
void main() {
    vec2 texel=1.0/vec2(textureSize(source,0));
    vec3 sum = vec3(0.0);
    // A normalized 3x3 tent, progressively accumulated into the next larger mip.
    for(int y = -1; y <= 1; ++y) for(int x = -1; x <= 1; ++x) {
        float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0) / 16.0;
        sum += texture(source,uv+vec2(x,y)*texel).rgb*weight;
    }
    fragColor = vec4(sum, 0.0);
}
