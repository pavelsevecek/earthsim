#version 330 core
in vec3 worldPosition;
uniform vec3 sunDirection;
uniform vec3 eye;
uniform float daylight;
uniform float atmosphereOpacity;
out vec4 fragColor;
float hazeTransmittance(float distanceToEye) {
    float distanceBeyondClearAir=max(distanceToEye-300.0,0.0);
    float opticalDepth=pow(distanceBeyondClearAir/1200.0,2.4)*atmosphereOpacity;
    return exp(-opticalDepth);
}
void main() {
    const vec3 normal=vec3(0.0,1.0,0.0);
    vec3 directionToEye=normalize(eye-worldPosition);
    vec3 reflection=reflect(-directionToEye,normal);
    float skyHeight=smoothstep(-0.15,0.85,reflection.y);
    vec3 reflectedSky=mix(vec3(0.34,0.52,0.72),vec3(0.055,0.24,0.62),skyHeight)*daylight;
    float facing=max(dot(normal,directionToEye),0.0);
    float fresnel=0.02+0.98*pow(1.0-facing,5.0);
    float sunGlint=pow(max(dot(reflection,sunDirection),0.0),180.0)*daylight;
    vec3 color=vec3(0.002,0.006,0.009)+reflectedSky*(0.16+0.84*fresnel)
        +vec3(1.0,0.88,0.62)*sunGlint*2.5;
    float alpha=0.52*hazeTransmittance(length(eye-worldPosition));
    fragColor=vec4(color,alpha);
}
