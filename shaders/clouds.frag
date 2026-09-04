#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D sceneDepth;
uniform sampler3D noiseTexture;
uniform vec2 depthProjection;
uniform vec3 eye, cameraForward, cameraRight, cameraUp;
uniform vec3 sunDirection, fogColor;
uniform float aspect, tanHalfFov, daylight, atmosphereOpacity, time, cloudCoverage;
const vec3 boxMin = vec3(-2200.0, 290.0, -2200.0);
const vec3 boxMax = vec3(2200.0, 530.0, 2200.0);
const vec2 prevailingWind = normalize(vec2(0.85, 0.35));
const float cloudSpeed = 10;

float hazeAmount(float distanceToEye) {
    float distanceBeyondClearAir=max(distanceToEye-300.0,0.0);
    float opticalDepth=pow(distanceBeyondClearAir/1200.0,2.4)*atmosphereOpacity;
    return 1.0-exp(-opticalDepth);
}

float cloudNoise3D(vec3 p) {
    vec3 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return texture(noiseTexture, (cell + f + 0.5) / 64.0).r;
}
float density(vec3 p) {
    if(cloudCoverage <= 0.0001) return 0.0;
    if(any(lessThan(p, boxMin)) || any(greaterThan(p, boxMax))) return 0.0;
    float layer = (p.y - boxMin.y) / (boxMax.y - boxMin.y);
    float profile = smoothstep(0.0, 0.16, layer) * (1.0 - smoothstep(0.48, 1.0, layer));
    float edge = 1.0 - smoothstep(1700.0, 2200.0, max(abs(p.x), abs(p.z)));
    vec2 advected = p.xz - prevailingWind * (time * cloudSpeed);
    vec3 q = vec3(advected.x, p.y, advected.y) * 0.007;
    float shape = 0.60 * cloudNoise3D(q) + 0.28 * cloudNoise3D(q * 2.03 + 17.0) + 0.12 * cloudNoise3D(q * 4.11 + 31.0);
    float threshold=mix(0.68,0.18,cloudCoverage);
    return smoothstep(threshold,threshold+0.29,shape)*profile*edge;
}
bool intersectVolume(vec3 ray, out float entry, out float leave) {
    entry = 0.0; leave = 1e20;
    for(int axis = 0; axis < 3; ++axis) {
        if(abs(ray[axis]) < 1e-7) {
            if(eye[axis] < boxMin[axis] || eye[axis] > boxMax[axis]) return false;
        } else {
            float a = (boxMin[axis] - eye[axis]) / ray[axis];
            float b = (boxMax[axis] - eye[axis]) / ray[axis];
            entry = max(entry, min(a, b)); leave = min(leave, max(a, b));
        }
    }
    return leave > entry;
}
void main() {
    fragColor = vec4(0.0);
    if(atmosphereOpacity <= 0.0 || cloudCoverage <= 0.0001) return;
    vec2 screen = uv * 2.0 - 1.0;
    vec3 ray = normalize(cameraForward + tanHalfFov * (screen.x * aspect * cameraRight + screen.y * cameraUp));
    float entry, leave;
    if(!intersectVolume(ray, entry, leave)) return;
    float depth = texture(sceneDepth, uv).r;
    if(depth < 1.0) {
        float viewDistance = depthProjection.y / (depth * 2.0 - 1.0 + depthProjection.x);
        leave = min(leave, viewDistance / max(dot(ray, cameraForward), 0.0001));
    }
    if(leave <= entry) return;
    float stride = (leave - entry) / 64.0;
    // Stable spatial jitter avoids coherent ray-march bands without temporal flicker.
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec3 lightDirection = daylight > 0.1 ? sunDirection : -sunDirection;
    vec3 lightColor = mix(vec3(0.10, 0.14, 0.24), mix(vec3(1.0, 0.36, 0.13), vec3(1.0, 0.96, 0.88), smoothstep(0.0, 0.4, sunDirection.y)), daylight);
    vec3 ambient = mix(vec3(0.012, 0.02, 0.045), vec3(0.26, 0.34, 0.44), daylight);
    float forwardScatter = pow(max(dot(ray, lightDirection), 0.0), 8.0);
    float transmittance = 1.0;
    vec3 scattering = vec3(0.0);
    for(int i = 0; i < 64; ++i) {
        float t = entry + (float(i) + jitter) * stride;
        vec3 p = eye + ray * t;
        float d = density(p);
        if(d < 0.001) continue;
        float opticalDepth = 0.0;
        for(int j = 0; j < 3; ++j)
            opticalDepth += density(p + lightDirection * (float(j) + 0.5) * 65.0) * 65.0;
        vec3 lighting = ambient + lightColor * exp(-opticalDepth * 0.025) * (0.75 + 0.65 * forwardScatter);
        lighting = mix(lighting, fogColor, hazeAmount(t));
        float alpha = 1.0 - exp(-d * stride * 0.026 * atmosphereOpacity);
        scattering += transmittance * alpha * lighting;
        transmittance *= 1.0 - alpha;
        if(transmittance < 0.01) break;
    }
    fragColor = vec4(scattering, 1.0 - transmittance);
}
