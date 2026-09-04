#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D indirectLight;
uniform sampler2D sceneDepth;
uniform vec2 depthProjection;
float viewDistance(float depth) {
    return abs(depthProjection.y / (depth * 2.0 - 1.0 + depthProjection.x));
}
void main() {
    float receiverDistance = viewDistance(texture(sceneDepth, uv).r);
    vec2 size = vec2(textureSize(indirectLight, 0));
    vec2 pixel = uv * size - 0.5;
    vec2 fraction = fract(pixel);
    vec3 light = vec3(0.0);
    float total = 0.0;
    // Depth-aware upsampling prevents light leaking across mountain silhouettes.
    for(int y = 0; y < 2; ++y) for(int x = 0; x < 2; ++x) {
        vec2 sampleUv = (floor(pixel) + vec2(x,y) + 0.5) / size;
        vec4 sampleLight = texture(indirectLight, sampleUv);
        float weight = (x == 0 ? 1.0-fraction.x : fraction.x) * (y == 0 ? 1.0-fraction.y : fraction.y);
        weight *= exp(-abs(sampleLight.a - receiverDistance) / max(1.0, receiverDistance * 0.02));
        light += sampleLight.rgb * weight; total += weight;
    }
    fragColor = vec4(total > 0.0001 ? light / total : vec3(0.0), 0.0);
}
