#version 330 core
out vec2 uv;
void main() {
    const vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    vec2 p=positions[gl_VertexID];
    uv=p*0.5+0.5;
    gl_Position=vec4(p,0,1);
}
