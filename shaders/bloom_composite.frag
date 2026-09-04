#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D scene;
uniform sampler2D bloom;
uniform float bloomStrength;
uniform float exposure;
void main() {
    vec3 hdr = texture(scene, uv).rgb;
    if(bloomStrength > 0.0)
        hdr += texture(bloom,uv).rgb*bloomStrength;
    hdr = max(hdr*exposure,vec3(0.0));
    vec3 mapped = hdr / (vec3(1.0) + hdr);
    vec3 srgb = mix(12.92 * mapped, 1.055 * pow(mapped, vec3(1.0/2.4)) - 0.055, step(vec3(0.0031308), mapped));
    fragColor = vec4(srgb, 1.0);
}
