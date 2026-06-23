#version 450

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    vec3 lightPos;
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

    // Direction from current fragment to the lamp.
    vec3 lightVector = gubo.lightPos - fragWorldPos;
    float distanceToLight = length(lightVector);
    vec3 L = normalize(lightVector);

    // Direction from current fragment to the camera.
    vec3 V = normalize(gubo.eyePos - fragWorldPos);

    // Blinn-Phong halfway vector.
    vec3 H = normalize(L + V);

    vec3 baseColor = texture(texSampler, fragUV).rgb;

    // Distance attenuation for point light.
    float constantAtt = 1.0;
    float linearAtt = 0.18;
    float quadraticAtt = 0.055;

    float attenuation =
    1.0 / (
    constantAtt +
    linearAtt * distanceToLight +
    quadraticAtt * distanceToLight * distanceToLight
    );

    // Ambient term: simple indirect light approximation.
    float ambientStrength = 0.12;
    vec3 ambient = ambientStrength * baseColor;

    // Lambert diffuse term.
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * baseColor * gubo.lightColor.rgb * attenuation;

    // Blinn-Phong specular term.
    float specularStrength = 0.45;
    float shininess = 48.0;
    float spec = pow(max(dot(N, H), 0.0), shininess);
    vec3 specular = specularStrength * spec * gubo.lightColor.rgb * attenuation;

    vec3 finalColor = ambient + diffuse + specular;

    outColor = vec4(finalColor, 1.0);
}