#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D sceneDepth;
uniform sampler2D cloudShadowMap;
uniform sampler3D noiseTexture;
uniform sampler3D cloudDensityTexture;
uniform vec2 depthProjection;
uniform vec3 eye, cameraForward, cameraRight, cameraUp;
uniform vec3 sunDirection, fogColor;
uniform float aspect, tanHalfFov, daylight, atmosphereOpacity, cloudOpacity, time, cloudCoverage;
uniform float windSpeed, cloudBase, cloudTop;
uniform vec2 windDirection;
uniform bool godRaysEnabled;
vec3 boxMin() { return vec3(-2200.0, cloudBase, -2200.0); }
vec3 boxMax() { return vec3(2200.0, cloudTop, 2200.0); }

float hazeAmount(float distanceToEye) {
    float distanceBeyondClearAir=max(distanceToEye-300.0,0.0);
    float opticalDepth=pow(distanceBeyondClearAir/1200.0,2.4)*atmosphereOpacity*0.025;
    return 1.0-exp(-opticalDepth);
}

float cloudNoise3D(vec3 p) {
    vec3 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return texture(noiseTexture, (cell + f + 0.5) / 64.0).r;
}
float density(vec3 p) {
    if(cloudCoverage <= 0.0001) return 0.0;
    if(any(lessThan(p, boxMin())) || any(greaterThan(p, boxMax()))) return 0.0;
    float layer = (p.y - cloudBase) / (cloudTop - cloudBase);
    float profile = smoothstep(0.0, 0.16, layer) * (1.0 - smoothstep(0.68, 1.0, layer));
    float edge = 1.0 - smoothstep(1700.0, 2200.0, max(abs(p.x), abs(p.z)));
    vec3 volumeUv=(p-boxMin())/(boxMax()-boxMin());
    float simulatedShape=texture(cloudDensityTexture,volumeUv).r;
    vec2 advected=p.xz-windDirection*(time*windSpeed*0.17);
    float fineDetail=cloudNoise3D(vec3(advected.x,p.y,advected.y)*0.029+19.0);
    float shape=clamp(simulatedShape+(fineDetail-0.5)*0.10,0.0,1.0);
    float threshold=mix(0.68,0.18,cloudCoverage);
    return smoothstep(threshold,threshold+0.29,shape)*profile*edge;
}
bool intersectVolume(vec3 ray, out float entry, out float leave) {
    entry = 0.0; leave = 1e20;
    for(int axis = 0; axis < 3; ++axis) {
        if(abs(ray[axis]) < 1e-7) {
            if(eye[axis] < boxMin()[axis] || eye[axis] > boxMax()[axis]) return false;
        } else {
            float a = (boxMin()[axis] - eye[axis]) / ray[axis];
            float b = (boxMax()[axis] - eye[axis]) / ray[axis];
            entry = max(entry, min(a, b)); leave = min(leave, max(a, b));
        }
    }
    return leave > entry;
}

vec3 godRayScattering(vec3 ray, float rayLimit, float jitter) {
    if(!godRaysEnabled || sunDirection.y <= 0.03 || daylight <= 0.01)
        return vec3(0.0);

    float fadeDistance = max(0.5 * (cloudTop - cloudBase), 1.0);
    float heightFade = 1.0 - smoothstep(cloudTop - fadeDistance, cloudTop, eye.y);
    if(heightFade <= 0.0) return vec3(0.0);

    float leave = min(rayLimit, 3500.0);
    if(ray.y > 0.0001)
        leave = min(leave, (cloudBase - eye.y) / ray.y);
    if(leave <= 0.0) return vec3(0.0);

    const int steps = 12;
    float stride = leave / float(steps);
    float g = 0.65;
    float mu = clamp(dot(ray, sunDirection), -1.0, 1.0);
    float phase = (1.0 - g*g)
        / (12.5663706 * pow(max(1.0 + g*g - 2.0*g*mu, 0.001), 1.5));
    float sunlight = smoothstep(0.03, 0.18, sunDirection.y) * daylight * heightFade;
    vec3 sunColor = mix(vec3(1.0, 0.36, 0.13), vec3(1.0, 0.96, 0.88),
        smoothstep(0.0, 0.4, sunDirection.y));
    vec3 scattering = vec3(0.0);
    float transmittance = 1.0;
    for(int i = 0; i < steps; ++i) {
        float t = (float(i) + jitter) * stride;
        vec3 p = eye + ray * t;

        // The shadow texture describes visibility at ground receivers. Project
        // this air sample down the sunlight ray to the same approximate footprint.
        vec2 receiver = p.xz - sunDirection.xz * p.y / max(sunDirection.y, 0.03);
        float cloudVisibility = texture(cloudShadowMap, receiver / 2000.0 + 0.5).r;
        float localDensity = exp(-max(p.y, 0.0) / 500.0);
        float stepTransmittance = exp(-stride * 0.001 * atmosphereOpacity * localDensity);
        scattering += transmittance * (1.0 - stepTransmittance)
            * cloudVisibility * phase * sunlight * sunColor * 50.0;
        transmittance *= stepTransmittance;
    }
    return scattering;
}

void main() {
    fragColor = vec4(0.0);
    if(cloudOpacity <= 0.0 || cloudCoverage <= 0.0001) return;
    vec2 screen = uv * 2.0 - 1.0;
    vec3 ray = normalize(cameraForward + tanHalfFov * (screen.x * aspect * cameraRight + screen.y * cameraUp));
    float depth = texture(sceneDepth, uv).r;
    float rayLimit = 1e20;
    if(depth < 1.0) {
        float viewDistance = depthProjection.y / (depth * 2.0 - 1.0 + depthProjection.x);
        rayLimit = viewDistance / max(dot(ray, cameraForward), 0.0001);
    }
    // Stable spatial jitter avoids coherent ray-march bands without temporal flicker.
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec3 airScattering = godRayScattering(ray, rayLimit, jitter);
    fragColor = vec4(airScattering, 0.0);
    float entry, leave;
    if(!intersectVolume(ray, entry, leave)) return;
    leave = min(leave, rayLimit);
    if(leave <= entry) return;
    float stride = (leave - entry) / 64.0;
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
        float alpha = 1.0 - exp(-d * stride * 0.026 * cloudOpacity);
        scattering += transmittance * alpha * lighting;
        transmittance *= 1.0 - alpha;
        if(transmittance < 0.01) break;
    }
    fragColor = vec4(airScattering + scattering, 1.0 - transmittance);
}
