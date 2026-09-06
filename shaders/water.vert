#version 330 core
uniform mat4 viewProjection;
uniform float waterLevel;
uniform sampler2D waterState;
out vec3 worldPosition;
out vec3 surfaceNormal;
void main() {
    const int resolution=256;
    const float cellSize=2000.0/float(resolution-1);
    ivec2 cell=ivec2(gl_VertexID%resolution,gl_VertexID/resolution);
    vec2 uv=vec2(cell)/float(resolution-1);
    vec2 xz=uv*2000.0-1000.0;
    float displacement=texelFetch(waterState,cell,0).r;
    int left=max(cell.x-1,0),right=min(cell.x+1,resolution-1);
    int down=max(cell.y-1,0),up=min(cell.y+1,resolution-1);
    float hLeft=texelFetch(waterState,ivec2(left,cell.y),0).r;
    float hRight=texelFetch(waterState,ivec2(right,cell.y),0).r;
    float hDown=texelFetch(waterState,ivec2(cell.x,down),0).r;
    float hUp=texelFetch(waterState,ivec2(cell.x,up),0).r;
    surfaceNormal=normalize(vec3(hLeft-hRight,2.0*cellSize,hDown-hUp));
    worldPosition=vec3(xz.x,waterLevel+displacement,xz.y);
    gl_Position=viewProjection*vec4(worldPosition,1.0);
}
