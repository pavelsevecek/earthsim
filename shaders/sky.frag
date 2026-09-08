#version 330 core
in vec2 screen;
uniform vec3 cameraForward;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform float aspect;
uniform float tanHalfFov;
uniform vec3 sunDirection;
uniform vec3 celestialPole;
uniform float daylight;
uniform vec3 fogColor;
uniform float atmosphereOpacity;
uniform float time;
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

float auroraNoise(vec2 p) {
    vec2 cell=floor(p),f=fract(p);
    f=f*f*(3.0-2.0*f);
    float a=starRandom(uint(cell.x+4096.0)+uint(cell.y+4096.0)*8192u);
    float b=starRandom(uint(cell.x+4097.0)+uint(cell.y+4096.0)*8192u);
    float c=starRandom(uint(cell.x+4096.0)+uint(cell.y+4097.0)*8192u);
    float d=starRandom(uint(cell.x+4097.0)+uint(cell.y+4097.0)*8192u);
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}

float auroraFbm(vec2 p) {
    float value=0.0;
    value+=0.57*auroraNoise(p);
    value+=0.28*auroraNoise(p*2.13+vec2(13.7,7.1));
    value+=0.15*auroraNoise(p*4.37+vec2(31.9,19.3));
    return value;
}

vec3 aurora(vec3 ray) {
    // A fixed world-space magnetic north keeps the aurora attached to Earth
    // while the celestial sphere turns through the day-night cycle.
    float horizontal=max(length(ray.xz),0.0001);
    float azimuth=atan(ray.x,ray.z);
    float elevation=atan(ray.y,horizontal);
    float north=smoothstep(-0.30,0.42,ray.z);
    float vertical=smoothstep(0.015,0.12,ray.y)*(1.0-smoothstep(0.82,0.99,ray.y));

    float drift=time*0.62;
    float broadNoise=auroraFbm(vec2(azimuth*2.8+drift*0.21,elevation*3.2-drift*0.17));
    float detailNoise=auroraFbm(vec2(azimuth*8.5-drift*0.34,elevation*7.0+drift*0.29));
    float warp=0.55*sin(azimuth*2.7-drift*0.48)
        +0.30*sin(azimuth*7.1+elevation*4.0+drift*0.73)
        +(broadNoise-0.5)*2.3+(detailNoise-0.5)*0.85;
    float phase=azimuth*10.0+warp+drift*0.16;
    float fold0=sin(phase),fold1=sin(phase*0.73+1.8);
    float curtain=exp(-38.0*fold0*fold0);
    curtain+=0.62*exp(-52.0*fold1*fold1);
    float rayNoise=auroraFbm(vec2(azimuth*21.0+drift*0.55,elevation*5.0-drift*0.43));
    float fineRays=0.35+0.65*pow(0.5+0.5*sin(azimuth*91.0-elevation*7.0+drift*2.1+rayNoise*5.0),3.0);
    float heightPulse=0.55+0.45*auroraFbm(vec2(elevation*8.0-drift*0.51,azimuth*5.0+drift*0.37));
    float flicker=0.72+0.28*sin(drift*3.4+azimuth*17.0+detailNoise*8.0);
    float intensity=curtain*fineRays*heightPulse*flicker*north*vertical;

    float upper=smoothstep(0.28,0.78,ray.y);
    vec3 lowerColor=vec3(0.05,1.15,0.42);
    vec3 upperColor=vec3(0.48,0.12,0.95);
    return mix(lowerColor,upperColor,upper)*intensity;
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
    vec3 celestialUp = normalize(cross(celestialPole, sunDirection));
    vec3 starRay = vec3(dot(ray, sunDirection), dot(ray, celestialUp), dot(ray, celestialPole));
    float starVisibility=exp(-10.0*daylight*atmosphereOpacity);
    color += vec3(fullSkyStars(starRay)) * starVisibility;
    // The pole's elevation equals observer latitude; northern lights fade over
    // ten degrees at each edge of the 50-80 degree northern latitude band.
    float latitude = degrees(asin(clamp(celestialPole.y, -1.0, 1.0)));
    float auroraLatitude = smoothstep(50.0, 60.0, latitude)
        * (1.0 - smoothstep(70.0, 80.0, latitude));
    float auroraVisibility=pow(1.0-daylight,3.0)*smoothstep(0.03,0.30,atmosphereOpacity)*auroraLatitude;
    color += aurora(ray)*auroraVisibility;
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
