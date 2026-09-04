#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D source;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(source, 0));
    vec3 sum = vec3(0.0);
    vec3 a=texture(source,uv+vec2(-2, 2)*texel).rgb;
    vec3 b=texture(source,uv+vec2( 0, 2)*texel).rgb;
    vec3 c=texture(source,uv+vec2( 2, 2)*texel).rgb;
    vec3 d=texture(source,uv+vec2(-2, 0)*texel).rgb;
    vec3 e=texture(source,uv).rgb;
    vec3 f=texture(source,uv+vec2( 2, 0)*texel).rgb;
    vec3 g=texture(source,uv+vec2(-2,-2)*texel).rgb;
    vec3 h=texture(source,uv+vec2( 0,-2)*texel).rgb;
    vec3 i=texture(source,uv+vec2( 2,-2)*texel).rgb;
    vec3 j=texture(source,uv+vec2(-1, 1)*texel).rgb;
    vec3 k=texture(source,uv+vec2( 1, 1)*texel).rgb;
    vec3 l=texture(source,uv+vec2(-1,-1)*texel).rgb;
    vec3 m=texture(source,uv+vec2( 1,-1)*texel).rgb;
    sum=e*0.125;
    sum+=(a+c+g+i)*0.03125;
    sum+=(b+d+f+h)*0.0625;
    sum+=(j+k+l+m)*0.125;
    fragColor=vec4(max(sum,vec3(0.0)),1.0);
}
