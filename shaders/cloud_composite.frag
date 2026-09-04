#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D sceneColor, sceneDepth, cloudColor;
uniform vec2 depthProjection;
float viewDistance(float depth) {
    return depthProjection.y / (depth * 2.0 - 1.0 + depthProjection.x);
}
void main() {
    vec2 size = vec2(textureSize(cloudColor, 0));
    vec2 pixel = uv * size - 0.5;
    vec2 fraction = fract(pixel);
    float depth = texture(sceneDepth, uv).r;
    float distance = viewDistance(depth);
    vec4 cloud = vec4(0.0);
    float total = 0.0;
    // Bilateral upsampling keeps half-resolution clouds off foreground silhouettes.
    for(int y = 0; y < 2; ++y) for(int x = 0; x < 2; ++x) {
        vec2 sampleUv = (floor(pixel) + vec2(x,y) + 0.5) / size;
        float otherDepth = texture(sceneDepth, sampleUv).r;
        float weight = (x == 0 ? 1.0-fraction.x : fraction.x) * (y == 0 ? 1.0-fraction.y : fraction.y);
        if((depth < 1.0) != (otherDepth < 1.0)) weight = 0.0;
        else if(depth < 1.0) weight *= exp(-abs(viewDistance(otherDepth)-distance) / max(1.0, distance*0.02));
        cloud += texture(cloudColor, sampleUv) * weight;
        total += weight;
    }
    if(total > 0.0001) cloud /= total;
    else cloud = vec4(0.0);
    // Keep the scene in linear HDR until bloom and final display mapping.
    vec3 scene = texture(sceneColor, uv).rgb;
    vec3 color = cloud.rgb + scene * (1.0 - cloud.a);
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
