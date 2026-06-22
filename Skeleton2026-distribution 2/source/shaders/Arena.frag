#version 450

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    vec3 lightDir;
    vec4 lightColor;
    vec3 eyePos;
} gubo;

layout(set = 1, binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-gubo.lightDir);
    vec3 V = normalize(gubo.eyePos - fragWorldPos);
    vec3 H = normalize(L + V);

    vec3 texColor = texture(texSampler, fragUV).rgb;

    float ambientStrength = 0.18;
    vec3 ambient = ambientStrength * texColor;

    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * texColor * gubo.lightColor.rgb;

    float specularStrength = 0.35;
    float shininess = 32.0;
    float spec = pow(max(dot(N, H), 0.0), shininess);
    vec3 specular = specularStrength * spec * gubo.lightColor.rgb;

    vec3 finalColor = ambient + diffuse + specular;

    outColor = vec4(finalColor, 1.0);
}