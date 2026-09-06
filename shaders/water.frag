#version 330 core
in vec3 worldPosition;
in vec3 surfaceNormal;
uniform vec3 sunDirection;
uniform vec3 eye;
uniform vec3 fogColor;
uniform float daylight;
uniform float atmosphereOpacity;
uniform float time;
uniform sampler2D reflectionTexture;
uniform sampler2D terrainHeight;
uniform mat4 reflectionViewProjection;
uniform float waterLevel;
out vec4 fragColor;
float foamHash(vec2 cell) {
    return fract(sin(dot(cell,vec2(127.1,311.7)))*43758.5453);
}
float foamNoise(vec2 p) {
    vec2 cell=floor(p),f=fract(p);
    f=f*f*(3.0-2.0*f);
    float a=foamHash(cell),b=foamHash(cell+vec2(1,0));
    float c=foamHash(cell+vec2(0,1)),d=foamHash(cell+vec2(1,1));
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
float dielectricFresnel(float cosineIncident,float etaIncident,float etaTransmitted) {
    float sineTransmittedSquared=(etaIncident/etaTransmitted)*(etaIncident/etaTransmitted)
        *max(0.0,1.0-cosineIncident*cosineIncident);
    if(sineTransmittedSquared>=1.0) return 1.0;
    float cosineTransmitted=sqrt(max(0.0,1.0-sineTransmittedSquared));
    float perpendicular=(etaIncident*cosineIncident-etaTransmitted*cosineTransmitted)
        /(etaIncident*cosineIncident+etaTransmitted*cosineTransmitted);
    float parallel=(etaTransmitted*cosineIncident-etaIncident*cosineTransmitted)
        /(etaTransmitted*cosineIncident+etaIncident*cosineTransmitted);
    return 0.5*(perpendicular*perpendicular+parallel*parallel);
}
float hazeAmount(float distanceToEye) {
    float distanceBeyondClearAir=max(distanceToEye-300.0,0.0);
    float opticalDepth=pow(distanceBeyondClearAir/1200.0,2.4)*atmosphereOpacity*0.025;
    return 1.0-exp(-opticalDepth);
}
void main() {
    vec3 normal=normalize(surfaceNormal);
    vec3 toEye=eye-worldPosition;
    float distanceToEye=length(toEye);
    vec3 directionToEye=toEye/max(distanceToEye,0.0001);
    vec3 reflection=reflect(-directionToEye,normal);
    float skyHeight=smoothstep(-0.15,0.85,reflection.y);
    vec3 reflectedSky=mix(vec3(0.34,0.52,0.72),vec3(0.055,0.24,0.62),skyHeight)*daylight;
    vec4 reflectedClip=reflectionViewProjection*vec4(worldPosition,1.0);
    vec2 planarUv=reflectedClip.xy/max(abs(reflectedClip.w),0.0001)*0.5+0.5;
    // Project a point along the locally reflected view ray into the existing
    // planar capture. For flat water this remains aligned with planarUv; tilted
    // wave normals bend the lookup in screen space with the correct perspective.
    float reflectionDistance=clamp(distanceToEye*0.75,250.0,1200.0);
    vec3 reflectedTarget=worldPosition+reflection*reflectionDistance;
    vec4 localReflectionClip=reflectionViewProjection*vec4(reflectedTarget,1.0);
    vec2 localReflectionUv=localReflectionClip.xy
        /max(abs(localReflectionClip.w),0.0001)*0.5+0.5;
    vec2 reflectionOffset=localReflectionUv-planarUv;
    float offsetLength=length(reflectionOffset);
    reflectionOffset*=min(1.0,0.08/max(offsetLength,0.0001));
    vec2 reflectionUv=planarUv+reflectionOffset*step(0.0001,localReflectionClip.w);
    float edge=min(min(reflectionUv.x,reflectionUv.y),min(1.0-reflectionUv.x,1.0-reflectionUv.y));
    float inside=smoothstep(0.0,0.025,edge)*step(0.0001,reflectedClip.w);
    vec3 planarReflection=texture(reflectionTexture,clamp(reflectionUv,vec2(0.001),vec2(0.999))).rgb;
    vec3 reflectedColor=mix(reflectedSky,planarReflection,inside);
    bool viewedFromAir=directionToEye.y>=0.0;
    float facing=abs(dot(normal,directionToEye));
    float fresnel=dielectricFresnel(facing,viewedFromAir?1.0:1.333,viewedFromAir?1.333:1.0);
    float sunGlint=pow(max(dot(reflection,sunDirection),0.0),180.0)*daylight;
    vec3 reflectedRadiance=reflectedColor+vec3(1.0,0.88,0.62)*sunGlint*2.5;
    // Approximate a deep water column. Fresnel accounts for interface reflection;
    // Beer-Lambert extinction accounts for light that enters but does not return.
    float transmission=exp(-1.10/max(facing,0.08));
    vec3 waterScattering=mix(vec3(0.001,0.006,0.012),vec3(0.025,0.24,0.46),daylight);
    float alpha=1.0-(1.0-fresnel)*transmission;
    vec3 color=(fresnel*reflectedRadiance
        +(1.0-fresnel)*(1.0-transmission)*waterScattering)/max(alpha,0.0001);
    float groundHeight=texture(terrainHeight,worldPosition.xz/2000.0+0.5).r;
    float waterDepth=worldPosition.y-groundHeight;
    vec2 foamPosition=worldPosition.xz*0.085;
    float broad=foamNoise(foamPosition+vec2(time*0.10,-time*0.07));
    float detail=foamNoise(foamPosition*2.7+vec2(-time*0.19,time*0.13));
    float brokenPattern=0.68*broad+0.32*detail;
    float advancingWave=0.5+0.5*sin(waterDepth*1.35-time*1.6+broad*5.0);
    float shoreline=smoothstep(0.05,0.9,waterDepth)*(1.0-smoothstep(2.0,11.0,waterDepth));
    float foam=shoreline*smoothstep(0.38,0.68,brokenPattern*0.72+advancingWave*0.28);
    vec3 foamColor=mix(vec3(0.025,0.04,0.065),vec3(0.78,0.90,0.96),daylight);
    color=mix(color,foamColor,foam*0.92);
    alpha=1.0-(1.0-alpha)*(1.0-foam*0.86);
    // Compose atmospheric haze over both the water surface and whatever remains
    // visible through it, expressed again as a straight-alpha source layer.
    float haze=hazeAmount(distanceToEye);
    float hazedAlpha=haze+(1.0-haze)*alpha;
    color=(haze*fogColor+(1.0-haze)*alpha*color)/max(hazedAlpha,0.0001);
    alpha=hazedAlpha;
    fragColor=vec4(color,alpha);
}
