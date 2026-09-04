#version 330 core
in vec2 screen;
uniform vec3 cameraForward;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform float aspect;
uniform float tanHalfFov;
uniform vec3 sunDirection;
uniform float daylight;
uniform vec3 fogColor;
uniform float atmosphereOpacity;
out vec4 fragColor;
uint starHash(uint value) {
    value ^= value >> 16u; value *= 0x7feb352du;
    value ^= value >> 15u; value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}
float starRandom(uint value) {
    return float(starHash(value) >> 8u) * (1.0 / 16777216.0);
}
float fullSkyStars(vec3 direction) {
    // Cover all six cube faces, including the poles and the lower hemisphere.
    // One jittered star per tile avoids empty regions from thresholding a sine hash.
    vec3 magnitude = abs(direction);
    vec2 facePosition;
    uint face;
    if(magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
        facePosition = direction.yz / magnitude.x;
        face = direction.x >= 0.0 ? 0u : 1u;
    } else if(magnitude.y >= magnitude.z) {
        facePosition = direction.xz / magnitude.y;
        face = direction.y >= 0.0 ? 2u : 3u;
    } else {
        facePosition = direction.xy / magnitude.z;
        face = direction.z >= 0.0 ? 4u : 5u;
    }
    vec2 grid = (facePosition * 0.5 + 0.5) * 32.0;
    ivec2 tile = ivec2(floor(grid));
    float pixel = clamp(max(length(dFdx(grid)), length(dFdy(grid))), 0.002, 0.12);
    float light = 0.0;
    for(int y = -1; y <= 1; ++y) for(int x = -1; x <= 1; ++x) {
        ivec2 cell = tile + ivec2(x,y);
        if(any(lessThan(cell, ivec2(0))) || any(greaterThanEqual(cell, ivec2(32)))) continue;
        uint seed = uint(cell.x) + uint(cell.y) * 32u + face * 1024u;
        vec2 center = vec2(cell) + 0.12 + 0.76 * vec2(starRandom(seed * 3u), starRandom(seed * 3u + 1u));
        float brightness = starRandom(seed * 3u + 2u);
        float radius = mix(0.009, 0.023, brightness);
        float distanceToStar = length(grid - center);
        float coverage = 1.0 - smoothstep(max(0.0, radius - pixel), radius + pixel, distanceToStar);
        light += coverage * mix(0.25, 0.85, brightness);
    }
    return light;
}
void main() {
    vec3 ray = normalize(cameraForward + tanHalfFov * (screen.x * aspect * cameraRight + screen.y * cameraUp));
    vec3 zenith = mix(vec3(0.002, 0.004, 0.015), vec3(0.055, 0.22, 0.48), daylight);
    vec3 color = mix(fogColor, zenith, pow(max(ray.y, 0.0), 0.45));
    float sunset = exp(-abs(sunDirection.y) * 10.0);
    color += vec3(0.50, 0.10, 0.025) * sunset * pow(max(dot(ray, sunDirection), 0.0), 8.0);
    color *= atmosphereOpacity;
    float sun = smoothstep(cos(0.012), cos(0.009), dot(ray, sunDirection));
    color += vec3(2.0, 1.5, 0.85) * sun * smoothstep(-0.04, 0.01, sunDirection.y);
    float moon = smoothstep(cos(0.010), cos(0.007), dot(ray, -sunDirection));
    color += vec3(0.50, 0.58, 0.72) * moon * (1.0 - daylight);
    // Use the sun's orbital plane as a rotating celestial frame. Deriving it
    // from sunDirection keeps stars locked to both sun and moon, including pause.
    vec3 celestialPole = normalize(vec3(-0.30, 0.0, 1.0));
    vec3 celestialUp = normalize(cross(celestialPole, sunDirection));
    vec3 starRay = vec3(dot(ray, sunDirection), dot(ray, celestialUp), dot(ray, celestialPole));
    float starVisibility=exp(-10.0*daylight*atmosphereOpacity);
    color += vec3(fullSkyStars(starRay)) * starVisibility;
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
