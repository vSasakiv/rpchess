#version 450

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 lightViewProj;
    vec4 lightPos;
    vec4 lightColor;
    vec4 eyePos;
    vec4 shadowParams;
} gubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;

layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 mvpMat;
    mat4 mMat;
    vec4 materialColor;
} ubo;

layout(set = 1, binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

float computeShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir) {
    vec4 lightClip = gubo.lightViewProj * vec4(worldPos, 1.0);
    vec3 projCoords = lightClip.xyz / lightClip.w;

    // Outside spotlight frustum: do not shadow it.
    if (projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;

    if (
    shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
    shadowUV.y < 0.0 || shadowUV.y > 1.0
    ) {
        return 1.0;
    }

    float currentDepth = projCoords.z;

    float baseBias = gubo.shadowParams.x;
    float shadowStrength = gubo.shadowParams.y;

    float angleBias = max(baseBias * (1.0 - dot(normal, lightDir)), baseBias * 0.35);

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));

    float shadowAmount = 0.0;

    // 3x3 PCF filter for softer shadow edges.
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float closestDepth = texture(
                shadowMap,
                shadowUV + vec2(x, y) * texelSize
            ).r;

            if (currentDepth - angleBias > closestDepth) {
                shadowAmount += 1.0;
            }
        }
    }

    shadowAmount /= 9.0;

    return 1.0 - shadowAmount * shadowStrength;
}

void main() {
    vec3 N = normalize(fragNormal);

    vec3 lightVector = gubo.lightPos.xyz - fragWorldPos;
    float distanceToLight = length(lightVector);
    vec3 L = normalize(lightVector);

    vec3 V = normalize(gubo.eyePos.xyz - fragWorldPos);
    vec3 H = normalize(L + V);

    vec3 texColor = texture(texSampler, fragUV).rgb;
    vec3 baseColor = mix(texColor, ubo.materialColor.rgb, ubo.materialColor.a);

    float constantAtt = 1.0;
    float linearAtt = 0.18;
    float quadraticAtt = 0.055;

    float attenuation =
    1.0 / (
    constantAtt +
    linearAtt * distanceToLight +
    quadraticAtt * distanceToLight * distanceToLight
    );

    // Ambient approximates indirect light, so it remains visible in shadow.
    float ambientStrength = 0.13;
    vec3 ambient = ambientStrength * baseColor;

    // Lambert diffuse.
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * baseColor * gubo.lightColor.rgb * attenuation;

    // Blinn-Phong specular.
    float specularStrength = 0.40;
    float shininess = 48.0;
    float spec = pow(max(dot(N, H), 0.0), shininess);
    vec3 specular = specularStrength * spec * gubo.lightColor.rgb * attenuation;

    float visibility = computeShadowVisibility(fragWorldPos, N, L);

    vec3 finalColor = ambient + visibility * (diffuse + specular);

    outColor = vec4(finalColor, 1.0);
}