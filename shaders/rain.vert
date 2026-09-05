#version 430 core
struct RainDrop { vec4 positionAge; vec4 velocityState; };
layout(std430,binding=0) readonly buffer RainDrops { RainDrop drops[]; };
uniform mat4 viewProjection;
uniform vec3 eye;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform bool clipEnabled;
uniform float clipHeight;
uniform float clipDirection;
out vec2 local;
out float age;
flat out int particleState;
void main() {
    gl_ClipDistance[0]=1.0;
    const vec2 corners[6]=vec2[6](vec2(0,-1),vec2(1,-1),vec2(1,1),
        vec2(0,-1),vec2(1,1),vec2(0,1));
    RainDrop drop=drops[gl_InstanceID];
    particleState=int(drop.velocityState.w+0.5);
    age=drop.positionAge.w;
    local=vec2(corners[gl_VertexID].y,corners[gl_VertexID].x*2.0-1.0);
    if(particleState==0) { gl_Position=vec4(2,2,2,1); return; }
    vec3 world;
    if(particleState==1) {
        vec3 direction=normalize(drop.velocityState.xyz);
        vec3 side=cross(direction,normalize(eye-drop.positionAge.xyz));
        side=dot(side,side)>0.00001?normalize(side):cameraRight;
        world=drop.positionAge.xyz-direction*corners[gl_VertexID].x*8.0
            +side*corners[gl_VertexID].y*0.12;
    } else if(particleState==2) {
        float size=2.0+min(age*0.8,3.0);
        vec2 corner=vec2(corners[gl_VertexID].y,corners[gl_VertexID].x*2.0-1.0);
        local=corner;
        world=drop.positionAge.xyz+(cameraRight*corner.x+cameraUp*corner.y)*size;
    } else {
        vec2 corner=vec2(corners[gl_VertexID].y,corners[gl_VertexID].x*2.0-1.0);
        local=corner;
        float radius=0.5+age*5.5;
        world=drop.positionAge.xyz+vec3(corner.x*radius,0.03,corner.y*radius);
    }
    gl_ClipDistance[0]=clipEnabled?(world.y-clipHeight)*clipDirection:1.0;
    gl_Position=viewProjection*vec4(world,1.0);
}
