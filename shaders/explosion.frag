#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D sceneDepth;
uniform sampler3D smokeTexture;
uniform sampler3D temperatureTexture;
uniform vec2 depthProjection;
uniform vec3 eye,cameraForward,cameraRight,cameraUp;
uniform vec3 sunDirection;
uniform vec3 boxMin,boxMax;
uniform float aspect,tanHalfFov,daylight,atmosphereOpacity,age,strength;

bool intersectVolume(vec3 ray,out float entry,out float leave) {
    entry=0.0; leave=1e20;
    for(int axis=0;axis<3;++axis) {
        if(abs(ray[axis])<1e-7) {
            if(eye[axis]<boxMin[axis]||eye[axis]>boxMax[axis]) return false;
        } else {
            float a=(boxMin[axis]-eye[axis])/ray[axis];
            float b=(boxMax[axis]-eye[axis])/ray[axis];
            entry=max(entry,min(a,b)); leave=min(leave,max(a,b));
        }
    }
    return leave>entry;
}
float hazeTransmittance(float distanceToEye) {
    float clearDistance=max(distanceToEye-300.0,0.0);
    return exp(-pow(clearDistance/1200.0,2.4)*atmosphereOpacity*0.025);
}
float smokeAt(vec3 p) {
    vec3 q=(p-boxMin)/(boxMax-boxMin);
    if(any(lessThan(q,vec3(0.0)))||any(greaterThan(q,vec3(1.0)))) return 0.0;
    return texture(smokeTexture,q).r;
}
vec3 blackbodyRadiance(float heat) {
    vec3 deepRed=vec3(10.0,0.055,0.002);
    vec3 orange=vec3(22.0,2.0,0.05);
    vec3 yellow=vec3(34.0,14.0,1.5);
    vec3 warmWhite=vec3(42.0,35.0,20.0);
    if(heat<0.28)
        return mix(deepRed,orange,smoothstep(0.0,0.28,heat));
    if(heat<0.64)
        return mix(orange,yellow,smoothstep(0.28,0.64,heat));
    return mix(yellow,warmWhite,smoothstep(0.64,1.0,heat));
}
void main() {
    fragColor=vec4(0.0);
    vec2 screen=uv*2.0-1.0;
    vec3 ray=normalize(cameraForward+tanHalfFov*(screen.x*aspect*cameraRight+screen.y*cameraUp));
    float entry,leave;
    if(!intersectVolume(ray,entry,leave)) return;
    float depth=texture(sceneDepth,uv).r;
    if(depth<1.0) {
        float viewDistance=depthProjection.y/(depth*2.0-1.0+depthProjection.x);
        leave=min(leave,viewDistance/max(dot(ray,cameraForward),0.0001));
    }
    if(leave<=entry) return;
    float stride=(leave-entry)/72.0;
    float jitter=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))));
    vec3 lightDirection=daylight>0.1?sunDirection:-sunDirection;
    vec3 sunColor=mix(vec3(0.08,0.11,0.18),vec3(1.0,0.82,0.62),daylight);
    vec3 scattering=vec3(0.0);
    float transmittance=1.0;
    vec3 blastCenter=vec3((boxMin.x+boxMax.x)*0.5,boxMin.y+38.0*strength,
        (boxMin.z+boxMax.z)*0.5);
    float blastRadius=age*135.0*strength;
    float blastFade=1.0-smoothstep(0.15,1.35,age);
    for(int i=0;i<72;++i) {
        float t=entry+(float(i)+jitter)*stride;
        vec3 p=eye+ray*t;
        vec3 volumeUv=(p-boxMin)/(boxMax-boxMin);
        float smoke=texture(smokeTexture,volumeUv).r;
        float shock=exp(-pow((length(p-blastCenter)-blastRadius)/(4.0+2.0*strength),2.0))
            *blastFade;
        if(smoke<0.002&&shock<0.002) continue;
        float temperature=texture(temperatureTexture,volumeUv).r;
        float visibleHeat=clamp(temperature/2.25,0.0,1.0);
        float lightDepth=0.0;
        for(int j=0;j<3;++j)
            lightDepth+=smokeAt(p+lightDirection*(float(j)+0.5)*18.0)*18.0;
        float illumination=mix(0.055,0.24,daylight)+exp(-lightDepth*0.055)*0.76*daylight;
        vec3 coolColor= mix(vec3(0.055,0.045,0.038),vec3(0.32,0.29,0.27),illumination);
        vec3 hotColor=blackbodyRadiance(visibleHeat)*10.0;
        vec3 cloudRadiance=mix(
            coolColor*sunColor,hotColor,smoothstep(0.015,0.22,visibleHeat))*3.6;
        vec3 radiance=mix(
            cloudRadiance,vec3(5.5,3.6,1.7)*(0.55+0.45*daylight),shock);
        radiance*=4.f * hazeTransmittance(t);
        float smokeExtinction=mix(0.005,0.01,smoothstep(0.025,0.24,visibleHeat));
        float alpha=1.0-exp(-(smoke*smokeExtinction+shock*0.035)*stride);
        scattering+=transmittance*alpha*radiance;
        transmittance*=1.0-alpha;
        if(transmittance<0.01) break;
    }
    float fade=1.0-smoothstep(75.0,90.0,age);
    fragColor=vec4(scattering*fade,(1.0-transmittance)*fade);
}
