#version 330 core
in float strength;
layout(location=0) out vec4 fragColor;
layout(location=1) out vec4 emissiveColor;
void main() {
    vec3 radiance=vec3(0.48,0.68,1.0)*1e3*strength;
    fragColor=vec4(radiance,1.0);
    emissiveColor=vec4(radiance,1.0);
}
