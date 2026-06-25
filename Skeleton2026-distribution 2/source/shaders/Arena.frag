#version 450

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 lightViewProj;

    vec4 lightPositions[4];
    vec4 lightColors[4];
    vec4 lightEnabled;

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
    float baseBias = gubo.shadowParams.x;
    float shadowStrength = gubo.shadowParams.y;
    float normalOffset = gubo.shadowParams.z;
    float pcfRadius = gubo.shadowParams.w;

    vec3 receiverPos = worldPos + normal * normalOffset;

    vec4 lightClip = gubo.lightViewProj * vec4(receiverPos, 1.0);
    vec3 projCoords = lightClip.xyz / lightClip.w;

    if (projCoords.z <= 0.0 || projCoords.z >= 1.0) {
        return 1.0;
    }

    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;

    if (
    shadowUV.x <= 0.0 || shadowUV.x >= 1.0 ||
    shadowUV.y <= 0.0 || shadowUV.y >= 1.0
    ) {
        return 1.0;
    }

    float currentDepth = projCoords.z;

    float ndotl = max(dot(normal, lightDir), 0.0);
    float bias = max(baseBias * (1.0 - ndotl), baseBias * 0.35);

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));

    float shadowAmount = 0.0;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize * pcfRadius;
            float closestDepth = texture(shadowMap, shadowUV + offset).r;

            if (currentDepth - bias > closestDepth) {
                shadowAmount += 1.0;
            }
        }
    }

    shadowAmount /= 9.0;

    return 1.0 - shadowAmount * shadowStrength;
}

vec3 computePointLight(
    vec4 lightPosition,
    vec4 lightColorAndIntensity,
    float enabled,
    float usesShadow,
    vec3 baseColor,
    vec3 normal,
    vec3 viewDir
) {
    if (enabled < 0.5) {
        return vec3(0.0);
    }

    vec3 lightPos = lightPosition.xyz;
    vec3 lightColor = lightColorAndIntensity.rgb;
    float lightIntensity = lightColorAndIntensity.a;

    vec3 lightVector = lightPos - fragWorldPos;
    float distanceToLight = length(lightVector);
    vec3 lightDir = normalize(lightVector);

    vec3 halfVector = normalize(lightDir + viewDir);

    float constantAtt = 1.0;
    float linearAtt = 0.18;
    float quadraticAtt = 0.055;

    float attenuation =
    1.0 / (
    constantAtt +
    linearAtt * distanceToLight +
    quadraticAtt * distanceToLight * distanceToLight
    );

    float diff = max(dot(normal, lightDir), 0.0);

    vec3 diffuse =
    diff *
    baseColor *
    lightColor *
    lightIntensity *
    attenuation;

    float specularStrength = 0.35;
    float shininess = 48.0;
    float spec = pow(max(dot(normal, halfVector), 0.0), shininess);

    vec3 specular =
    specularStrength *
    spec *
    lightColor *
    lightIntensity *
    attenuation;

    float visibility = 1.0;

    if (usesShadow > 0.5) {
        visibility = computeShadowVisibility(fragWorldPos, normal, lightDir);
    }

    return visibility * (diffuse + specular);
}

void main() {
    vec3 texColor = texture(texSampler, fragUV).rgb;

    // Alpha above 1.5 means emissive material.
    // We avoid storing this as a bool because some shader parsers complain.
    if (ubo.materialColor.a > 1.5) {
        vec3 emissiveColor = ubo.materialColor.rgb * 4.0;
        emissiveColor += texColor * 0.15;

        outColor = vec4(emissiveColor, 1.0);
        return;
    }

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(gubo.eyePos.xyz - fragWorldPos);

    float tintStrength = clamp(ubo.materialColor.a, 0.0, 1.0);
    vec3 baseColor = mix(texColor, ubo.materialColor.rgb, tintStrength);

    // Ambient approximates indirect/background light.
    // It remains even when all lamps are off, so the scene is still visible.
    float ambientStrength = 0.04;
    vec3 ambient = ambientStrength * baseColor;

    vec3 lighting = vec3(0.0);

    // Light 0 uses the existing shadow map.
    lighting += computePointLight(
        gubo.lightPositions[0],
        gubo.lightColors[0],
        gubo.lightEnabled.x,
        1.0,
        baseColor,
        N,
        V
    );

    // Lights 1-3 illuminate without shadows.
    lighting += computePointLight(
        gubo.lightPositions[1],
        gubo.lightColors[1],
        gubo.lightEnabled.y,
        0.0,
        baseColor,
        N,
        V
    );

    lighting += computePointLight(
        gubo.lightPositions[2],
        gubo.lightColors[2],
        gubo.lightEnabled.z,
        0.0,
        baseColor,
        N,
        V
    );

    lighting += computePointLight(
        gubo.lightPositions[3],
        gubo.lightColors[3],
        gubo.lightEnabled.w,
        0.0,
        baseColor,
        N,
        V
    );

    vec3 finalColor = ambient + lighting;

    // Avoid extreme over-bright values when all four lights are active.
    finalColor = finalColor / (finalColor + vec3(1.0));

    outColor = vec4(finalColor, 1.0);
}