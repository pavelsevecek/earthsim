#version 330 core
layout(location=0) in vec4 startStrength;
layout(location=1) in vec4 endWidth;
uniform mat4 viewProjection;
uniform vec3 eye;
uniform vec3 cameraRight;
uniform bool clipEnabled;
uniform float clipHeight;
uniform float clipDirection;
out float strength;
void main() {
    const vec2 corners[6]=vec2[6](vec2(0,-1),vec2(1,-1),vec2(1,1),
        vec2(0,-1),vec2(1,1),vec2(0,1));
    vec3 start=startStrength.xyz,end=endWidth.xyz;
    vec3 center=mix(start,end,corners[gl_VertexID].x);
    vec3 direction=normalize(end-start);
    vec3 side=cross(direction,normalize(eye-center));
    side=dot(side,side)>0.00001?normalize(side):cameraRight;
    vec3 world=center+side*corners[gl_VertexID].y*endWidth.w;
    strength=startStrength.w;
    gl_ClipDistance[0]=clipEnabled?(world.y-clipHeight)*clipDirection:1.0;
    gl_Position=viewProjection*vec4(world,1.0);
}
