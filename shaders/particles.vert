#version 430 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 properties;
layout(location = 2) in vec3 emissionColor;
struct Particle { vec4 positionAge; vec4 velocityLife; vec4 data; };
layout(std430,binding=0) readonly buffer ParticleBuffer { Particle particles[]; };
uniform mat4 viewProjection;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform vec3 eye;
uniform bool gpuParticles;
uniform int renderMode;
uniform float particleLifetime;
uniform sampler1D blackbodyColors;
uniform mat4 lightViewProjection;
uniform bool clipEnabled;
uniform float clipHeight;
uniform float clipDirection;
out vec2 local;
out vec3 radiance;
out float opacity;
out float distanceToEye;
out float emissiveFactor;
out float waterFactor;
out float vaporFactor;
out float trailFactor;
out vec3 directionToEye;
out vec4 shadowPosition;
out vec3 particleCenter;
void main() {
    gl_ClipDistance[0]=1.0;
    const vec2 corners[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
    local = corners[gl_VertexID];
    vec3 center=position;
    float size=properties.x;
    radiance=emissionColor;
    opacity=properties.y;
    emissiveFactor=1.0;
    waterFactor=0.0;
    vaporFactor=0.0;
    trailFactor=0.0;
    if(gpuParticles) {
        Particle particle=particles[gl_InstanceID];
        bool trail=(uint(particle.data.z)&8u)!=0u;
        bool water=(uint(particle.data.z)&16u)!=0u;
        bool vapor=(uint(particle.data.z)&32u)!=0u;
        bool selected=renderMode==0?(!trail&&!water&&!vapor):(renderMode==1?trail:(renderMode==2?water:vapor));
        if(particle.data.w<0.5 || !selected) {
            opacity=0.0;
            gl_Position=vec4(2.0,2.0,2.0,1.0);
            return;
        }
        center=particle.positionAge.xyz;
        float life=trail?particle.velocityLife.w:particleLifetime;
        float normalizedAge=clamp(particle.positionAge.w/max(life,0.001),0.0,1.0);
        float sizeScale=trail?(1.0+2.0*normalizedAge):(vapor?(1.0+min(particle.positionAge.w*0.08,2.0)):1.0);
        size=particle.data.x*sizeScale;
        float ageFade=1.0-smoothstep(0.8,1.0,normalizedAge);
        opacity=trail?pow(1.0-normalizedAge,2.0):ageFade;
        if(trail) {
            trailFactor=1.0;
            float colorFade=smoothstep(0.12,0.92,normalizedAge);
            radiance=mix(vec3(0.002,0.85,1.0),vec3(1.0,0.004,0.001),colorFade);
        } else if(water) {
            radiance=vec3(0.002,0.006,0.009);
            emissiveFactor=0.0;
            waterFactor=1.0;
        } else if(vapor) {
            radiance=vec3(0.82,0.86,0.9);
            opacity=0.3*ageFade;
            emissiveFactor=0.0;
            vaporFactor=1.0;
        } else {
            float temperatureCoordinate=clamp((particle.data.y-300.0)/1500.0,0.0,1.0);
            radiance=texture(blackbodyColors,temperatureCoordinate).rgb;
        }
    }
    distanceToEye = length(center - eye);
    particleCenter=center;
    directionToEye=normalize(eye-center);
    vec3 world = center + (cameraRight * local.x + cameraUp * local.y) * size * 0.5;
    gl_ClipDistance[0]=clipEnabled?(world.y-clipHeight)*clipDirection:1.0;
    shadowPosition=lightViewProjection*vec4(world,1.0);
    gl_Position = viewProjection * vec4(world, 1.0);
}
