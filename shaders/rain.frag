#version 430 core
in vec2 local;
in float age;
flat in int particleState;
uniform float daylight;
layout(location=0) out vec4 fragColor;
layout(location=1) out vec4 emissiveColor;
void main() {
    vec3 color; float alpha;
    if(particleState==1) {
        color=mix(vec3(0.12,0.17,0.23),vec3(0.58,0.72,0.84),daylight);
        // local.y runs from -1 at the falling drop to +1 at the top of its streak.
        alpha=0.34*clamp(0.5*(1.0-local.y),0.0,1.0);
    } else if(particleState==2) {
        float r2=dot(local,local); if(r2>1.0) discard;
        color=vec3(0.82,0.86,0.9)*mix(0.04,1.0,daylight);
        alpha=0.26*pow(max(1.0-r2,0.0),1.5)*(1.0-smoothstep(2.2,3.0,age));
    } else {
        float radius=length(local);
        float ring=1.0-smoothstep(0.06,0.16,abs(radius-0.78));
        if(radius>1.0||ring<=0.001) discard;
        color=vec3(0.22,0.45,0.62)*daylight;
        alpha=ring*0.32*(1.0-age);
    }
    fragColor=vec4(color,alpha);
    emissiveColor=vec4(0,0,0,alpha);
}
