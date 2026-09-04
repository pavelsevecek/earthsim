#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D sceneDepth;
uniform sampler2D sceneEmission;
uniform vec2 depthProjection;
float viewDistance(float depth) { return abs(depthProjection.y / (depth * 2.0 - 1.0 + depthProjection.x)); }
void main() {
    float receiverDepth = texture(sceneDepth, uv).r;
    if(receiverDepth >= 1.0) { fragColor = vec4(0.0); return; }
    float maxLod = floor(log2(float(max(textureSize(sceneEmission, 0).x, textureSize(sceneEmission, 0).y))));
    vec3 indirect = vec3(0.0);
    float totalWeight = 0.0;
    // Centered trilinear mip samples form one continuous irradiance field.
    // There are no offset copies of the source image.
    for(int i = 2; i <= 8; ++i) {
        float lod = min(float(i), maxLod);
        float weight = exp(-0.32 * float(i - 2));
        indirect += textureLod(sceneEmission, uv, lod).rgb * min(exp2(lod), 32.0) * weight;
        totalWeight += weight;
    }
    indirect = min(indirect * (0.10 / max(totalWeight, 0.0001)), vec3(3.0));
    fragColor = vec4(indirect, viewDistance(receiverDepth));
}
