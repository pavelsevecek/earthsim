#version 430 core
in vec3 worldPosition;
in vec3 worldNormal;
in vec4 shadowPosition[2];
uniform sampler2DArrayShadow terrainShadowMap;
uniform sampler2D cloudShadowMap;
uniform bool cloudShadowsEnabled;
uniform vec3 eye;
uniform vec3 sunDirection;
uniform float daylight;
uniform vec3 fogColor;
uniform float atmosphereOpacity;
uniform bool aircraftShadowEnabled;
uniform vec3 aircraftPosition;
uniform bool headlightsEnabled;
uniform vec3 headlightPosition[2];
uniform vec3 headlightDirection;
uniform usampler2D terrainScorched;
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
float detailHash(ivec3 cell) {
    uvec3 p=uvec3(cell);
    uint h=p.x*1597334677u+p.y*3812015801u+p.z*2798796415u;
    h=(h^(h>>16u))*2246822519u;
    h=(h^(h>>13u))*3266489917u;
    return float((h^(h>>16u))>>8u)/16777216.0;
}
// Value and analytic spatial gradient of smooth 3D noise. Using all three
// coordinates keeps cliff detail from stretching into vertical stripes.
vec4 detailNoise(vec3 p) {
    ivec3 cell=ivec3(floor(p));
    vec3 f=fract(p);
    vec3 u=f*f*f*(f*(f*6.0-15.0)+10.0);
    vec3 du=30.0*f*f*(f-1.0)*(f-1.0);
    float a=detailHash(cell),b=detailHash(cell+ivec3(1,0,0));
    float c=detailHash(cell+ivec3(0,1,0)),d=detailHash(cell+ivec3(1,1,0));
    float e=detailHash(cell+ivec3(0,0,1)),f1=detailHash(cell+ivec3(1,0,1));
    float g=detailHash(cell+ivec3(0,1,1)),h=detailHash(cell+ivec3(1,1,1));
    float low=mix(mix(a,b,u.x),mix(c,d,u.x),u.y);
    float high=mix(mix(e,f1,u.x),mix(g,h,u.x),u.y);
    vec3 gradient=vec3(
        mix(mix(b-a,d-c,u.y),mix(f1-e,h-g,u.y),u.z)*du.x,
        mix(mix(c-a,d-b,u.x),mix(g-e,h-f1,u.x),u.z)*du.y,
        (high-low)*du.z);
    return vec4(mix(low,high,u.z)-0.5,gradient);
}
vec4 terrainDetail(vec3 p) {
    float footprint=max(length(dFdx(p)),length(dFdy(p)));
    vec4 detail=vec4(0.0);
    float frequency=0.7,amplitude=0.6;
    for(int octave=0;octave<3;++octave) {
        // Fade unresolved octaves toward their mean before they can alias.
        float weight=amplitude*(1.0-smoothstep(0.2,0.65,footprint*frequency));
        vec4 sampleNoise=detailNoise(p*frequency+vec3(17.0,43.0,29.0)*float(octave));
        detail+=vec4(sampleNoise.x,sampleNoise.yzw*frequency)*weight;
        frequency*=3.0;
        amplitude*=0.45;
    }
    return detail;
}
float sampleScorched(vec2 uv) {
    ivec2 size=textureSize(terrainScorched,0);
    vec2 position=uv*vec2(size)-0.5;
    ivec2 cell=ivec2(floor(position));
    vec2 fraction=fract(position);
    ivec2 maximum=size-1;
    float a=float(texelFetch(terrainScorched,clamp(cell,ivec2(0),maximum),0).r);
    float b=float(texelFetch(terrainScorched,clamp(cell+ivec2(1,0),ivec2(0),maximum),0).r);
    float c=float(texelFetch(terrainScorched,clamp(cell+ivec2(0,1),ivec2(0),maximum),0).r);
    float d=float(texelFetch(terrainScorched,clamp(cell+ivec2(1,1),ivec2(0),maximum),0).r);
    return mix(mix(a,b,fraction.x),mix(c,d,fraction.x),fraction.y)/65535.0;
}
float hazeOpticalDepth(float distanceToEye) {
    float distanceBeyondClearAir=max(distanceToEye-300.0,0.0);
    return pow(distanceBeyondClearAir/1200.0,2.4)*atmosphereOpacity*0.025;
}
float terrainVisibility(int layer, vec3 geometricNormal, vec3 lightDirection) {
    vec3 p = shadowPosition[layer].xyz / shadowPosition[layer].w * 0.5 + 0.5;
    if(any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    float bias = max(0.001, 0.001 * (1.0 - abs(dot(geometricNormal, lightDirection))));
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
    vec4 detail=terrainDetail(worldPosition);
    float detailStrength=mix(mix(0.30,0.48,rocky),0.12,snow);
    albedo*=1.0+detail.x*detailStrength;
    vec2 wetUv=clamp(worldPosition.xz/2000.0+0.5,vec2(0.0),vec2(0.99999));
    uvec2 wetCell=uvec2(wetUv*256.0);
    float wetAmount=clamp(float(wetness[wetCell.y*256u+wetCell.x])/65535.0,0.0,1.0);
    albedo=mix(albedo,albedo*0.38+vec3(0.008,0.014,0.018),wetAmount*0.78);
    float scorchedAmount=clamp(sampleScorched(wetUv),0.0,1.0);
    albedo=mix(albedo,vec3(0.028,0.025,0.023),scorchedAmount);
    // Perturb lighting only; material slopes and shadow bias use the original normals.
    vec3 tangentGradient=detail.yzw-n*dot(n,detail.yzw);
    float bumpStrength=mix(0.16,0.055,snow)*mix(1.0,0.55,wetAmount);
    n=normalize(n-tangentGradient*bumpStrength);
    float direct = max(dot(n, sunDirection), 0.0) * smoothstep(-0.05, 0.14, sunDirection.y);
    vec3 sunlight = mix(vec3(1.0, 0.38, 0.14), vec3(1.0, 0.96, 0.84), smoothstep(0.0, 0.4, sunDirection.y));
    vec3 ambient = mix(vec3(0.045, 0.065, 0.12), vec3(0.28, 0.34, 0.40), daylight);
    float moonlight = max(dot(n, -sunDirection), 0.0) * (1.0 - daylight);
    float cloudVisibility=cloudShadowsEnabled
        ? texture(cloudShadowMap,worldPosition.xz/2000.0+0.5).r : 1.0;
    direct *= terrainVisibility(0, geometricNormal, sunDirection)*cloudVisibility;
    moonlight *= terrainVisibility(1, geometricNormal, -sunDirection);
    vec3 color = albedo * (ambient + sunlight * direct * 1.15 + vec3(0.07, 0.10, 0.18) * moonlight);
    vec3 viewDirection=normalize(eye-worldPosition);
    float wetHighlight=pow(max(dot(reflect(-sunDirection,n),viewDirection),0.0),72.0)
        *wetAmount*daylight*terrainVisibility(0,geometricNormal,sunDirection)*cloudVisibility;
    color+=sunlight*wetHighlight*0.7;
    if(aircraftShadowEnabled) {
        float aircraftHeight=aircraftPosition.y-worldPosition.y;
        float heightFactor=clamp(aircraftHeight/250.0,0.0,1.0);
        float radius=mix(5.0,14.0,heightFactor);
        float distanceFromShadow=length(worldPosition.xz-aircraftPosition.xz)/radius;
        float blob=1.0-smoothstep(0.1,1.0,distanceFromShadow);
        float altitudeFade=1.0-smoothstep(300.0,600.0,aircraftHeight);
        float belowAircraft=step(0.0,aircraftHeight);
        float opacity=mix(0.48,0.16,heightFactor)*blob*altitudeFade*belowAircraft;
        color*=1.0-opacity;
    }
    // Two chassis-mounted spotlights, fading on at dusk and off at dawn.
    float night=1.0-smoothstep(-0.08,0.12,sunDirection.y);
    if(headlightsEnabled && night>0.0) {
        for(int i=0;i<2;++i) {
            vec3 fromLamp=worldPosition-headlightPosition[i];
            float distanceToLamp=length(fromLamp);
            vec3 ray=fromLamp/max(distanceToLamp,0.001);
            float cone=smoothstep(0.82,0.95,dot(ray,headlightDirection));
            float rangeFade=1.0-smoothstep(70.0,110.0,distanceToLamp);
            float intensity=night*cone*rangeFade*24.0/(1.0+0.003*distanceToLamp*distanceToLamp);
            float diffuse=max(dot(n,-ray),0.0);
            float sheen=pow(max(dot(reflect(ray,n),viewDirection),0.0),48.0)*wetAmount*0.35;
            color+=(albedo*diffuse+vec3(sheen))*vec3(1.0,0.94,0.80)*intensity;
        }
    }
    float fog = 1.0 - exp(-hazeOpticalDepth(length(eye-worldPosition)));
    color = mix(color, fogColor, fog);
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
