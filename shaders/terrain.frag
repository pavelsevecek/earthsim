#version 430 core
in vec3 worldPosition;
in vec3 worldNormal;
in vec4 shadowPosition[2];
uniform sampler2DArrayShadow terrainShadowMap;
uniform vec3 eye;
uniform vec3 sunDirection;
uniform float daylight;
uniform vec3 fogColor;
uniform float atmosphereOpacity;
layout(std430,binding=7) readonly buffer TerrainWetness { uint wetness[]; };
out vec4 fragColor;
float terrainHash(vec2 cell) {
    return fract(sin(dot(cell,vec2(127.1,311.7)))*43758.5453);
}
float terrainNoise(vec2 position) {
    vec2 cell=floor(position),fraction=fract(position);
    fraction=fraction*fraction*(3.0-2.0*fraction);
    float a=terrainHash(cell);
    float b=terrainHash(cell+vec2(1.0,0.0));
    float c=terrainHash(cell+vec2(0.0,1.0));
    float d=terrainHash(cell+vec2(1.0,1.0));
    return mix(mix(a,b,fraction.x),mix(c,d,fraction.x),fraction.y);
}
float hazeOpticalDepth(float distanceToEye) {
    float distanceBeyondClearAir=max(distanceToEye-300.0,0.0);
    return pow(distanceBeyondClearAir/1200.0,2.4)*atmosphereOpacity;
}
float terrainVisibility(int layer, vec3 geometricNormal, vec3 lightDirection) {
    vec3 p = shadowPosition[layer].xyz / shadowPosition[layer].w * 0.5 + 0.5;
    if(any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    float bias = max(0.00006, 0.0003 * (1.0 - abs(dot(geometricNormal, lightDirection))));
    vec2 texel = 1.0 / vec2(textureSize(terrainShadowMap, 0).xy);
    float visibility = 0.0;
    for(int y = -1; y <= 1; ++y) for(int x = -1; x <= 1; ++x)
        visibility += texture(terrainShadowMap, vec4(p.xy + vec2(x,y) * texel, float(layer), p.z - bias));
    return visibility / 9.0;
}
void main() {
    vec3 n = normalize(worldNormal);
    vec3 geometricNormal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
    float grain=0.68*terrainNoise(worldPosition.xz*0.08)
        +0.32*terrainNoise(worldPosition.xz*0.22+vec2(19.0,37.0));
    vec3 grass = mix(vec3(0.12, 0.18, 0.10), vec3(0.25, 0.28, 0.16), grain);
    vec3 rock = mix(vec3(0.25, 0.24, 0.23), vec3(0.40, 0.37, 0.32), grain);
    float rocky = max(smoothstep(37.5, 75.0, worldPosition.y), 1.0 - smoothstep(0.55, 0.85, n.y));
    vec3 albedo = mix(grass, rock, rocky);
    float snow = smoothstep(77.5 + 6.0 * grain, 105.0, worldPosition.y) * smoothstep(0.48, 0.85, n.y);
    albedo = mix(albedo, vec3(0.88, 0.92, 0.95), snow);
    vec2 wetUv=clamp(worldPosition.xz/2000.0+0.5,vec2(0.0),vec2(0.99999));
    uvec2 wetCell=uvec2(wetUv*256.0);
    float wetAmount=clamp(float(wetness[wetCell.y*256u+wetCell.x])/65535.0,0.0,1.0);
    albedo=mix(albedo,albedo*0.38+vec3(0.008,0.014,0.018),wetAmount*0.78);
    float direct = max(dot(n, sunDirection), 0.0) * smoothstep(-0.05, 0.14, sunDirection.y);
    vec3 sunlight = mix(vec3(1.0, 0.38, 0.14), vec3(1.0, 0.96, 0.84), smoothstep(0.0, 0.4, sunDirection.y));
    vec3 ambient = mix(vec3(0.045, 0.065, 0.12), vec3(0.28, 0.34, 0.40), daylight);
    float moonlight = max(dot(n, -sunDirection), 0.0) * (1.0 - daylight);
    direct *= terrainVisibility(0, geometricNormal, sunDirection);
    moonlight *= terrainVisibility(1, geometricNormal, -sunDirection);
    vec3 color = albedo * (ambient + sunlight * direct * 1.15 + vec3(0.07, 0.10, 0.18) * moonlight);
    vec3 viewDirection=normalize(eye-worldPosition);
    float wetHighlight=pow(max(dot(reflect(-sunDirection,n),viewDirection),0.0),72.0)
        *wetAmount*daylight*terrainVisibility(0,geometricNormal,sunDirection);
    color+=sunlight*wetHighlight*0.7;
    float fog = 1.0 - exp(-hazeOpticalDepth(length(eye-worldPosition)));
    color = mix(color, fogColor, fog);
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
