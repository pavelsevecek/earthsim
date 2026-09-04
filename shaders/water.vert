#version 330 core
uniform mat4 viewProjection;
uniform float waterLevel;
out vec3 worldPosition;
void main() {
    const vec2 corners[6]=vec2[6](vec2(-1000,-1000),vec2(1000,-1000),vec2(1000,1000),
        vec2(-1000,-1000),vec2(1000,1000),vec2(-1000,1000));
    vec2 xz=corners[gl_VertexID];
    worldPosition=vec3(xz.x,waterLevel,xz.y);
    gl_Position=viewProjection*vec4(worldPosition,1.0);
}
