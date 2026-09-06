#version 430 core
in vec3 worldNormal;
in vec3 surfaceColor;
uniform vec3 sunDirection;
uniform float daylight;
uniform bool mirrored;
out vec4 fragColor;
void main() {
    vec3 normal = normalize(worldNormal) * (gl_FrontFacing != mirrored ? 1.0 : -1.0);
    float direct = max(dot(normal, sunDirection), 0.0) * daylight;
    float moonlight = max(dot(normal, -sunDirection), 0.0) * (1.0 - daylight);
    vec3 ambient = mix(vec3(0.08, 0.11, 0.18), vec3(0.32, 0.36, 0.40), daylight);
    vec3 light = ambient + vec3(1.0, 0.92, 0.78) * direct + vec3(0.08, 0.12, 0.22) * moonlight;
    fragColor = vec4(surfaceColor * light, 1.0);
}
