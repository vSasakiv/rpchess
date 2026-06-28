#version 450

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 lightViewProj[4];
    vec4 lightPositions[4];
    vec4 lightColors[4];
    vec4 lightEnabled;
    vec4 eyePos;
    vec4 shadowParams;
} gubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap0;
layout(set = 0, binding = 2) uniform sampler2D shadowMap1;
layout(set = 0, binding = 3) uniform sampler2D shadowMap2;
layout(set = 0, binding = 4) uniform sampler2D shadowMap3;

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


float computeShadowVisibility(
    sampler2D currentShadowMap,
    mat4 currentLightViewProj,
    vec3 worldPos,
    vec3 normal,
    vec3 lightDir
) {
    float baseBias = gubo.shadowParams.x;
    float shadowStrength = gubo.shadowParams.y;
    float normalOffset = gubo.shadowParams.z;

    vec3 offsetWorldPos = worldPos + normal * normalOffset;

    vec4 lightClip = currentLightViewProj * vec4(offsetWorldPos, 1.0);

    if (lightClip.w <= 0.0) {
        return 1.0;
    }

    vec3 projCoords = lightClip.xyz / lightClip.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (
    projCoords.x < 0.0 || projCoords.x > 1.0 ||
    projCoords.y < 0.0 || projCoords.y > 1.0 ||
    projCoords.z < 0.0 || projCoords.z > 1.0
    ) {
        return 1.0;
    }

    float currentDepth = projCoords.z;

    float angleBias = baseBias * (1.0 - max(dot(normal, lightDir), 0.0));
    float bias = max(baseBias * 0.35, angleBias);

    vec2 texelSize = 1.0 / vec2(textureSize(currentShadowMap, 0));

    float shadow = 0.0;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float closestDepth = texture(
                currentShadowMap,
                projCoords.xy + vec2(x, y) * texelSize
            ).r;

            if (currentDepth - bias > closestDepth) {
                shadow += 1.0;
            }
        }
    }

    shadow /= 9.0;

    return mix(1.0, 1.0 - shadowStrength, shadow);
}


vec3 computePointLight(
    vec4 lightPosition,
    vec4 lightColorAndIntensity,
    float enabled,
    sampler2D currentShadowMap,
    mat4 currentLightViewProj,
    vec3 baseColor,
    vec3 normal,
    vec3 viewDir
) {
    if (enabled < 0.5) {
        return vec3(0.0);
    }

    vec3 lightVector = lightPosition.xyz - fragWorldPos;
    float distanceToLight = length(lightVector);

    if (distanceToLight <= 0.001) {
        return vec3(0.0);
    }

    vec3 lightDir = normalize(lightVector);

    float attenuation =
    1.0 /
    (
    1.0 +
    0.18 * distanceToLight +
    0.035 * distanceToLight * distanceToLight
    );

    float diffuseAmount = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float specularAmount = pow(max(dot(normal, halfwayDir), 0.0), 32.0);

    vec3 lightColor = lightColorAndIntensity.rgb;
    float intensity = lightColorAndIntensity.w;

    float shadowVisibility = computeShadowVisibility(
        currentShadowMap,
        currentLightViewProj,
        fragWorldPos,
        normal,
        lightDir
    );

    vec3 diffuse = diffuseAmount * baseColor;
    vec3 specular = specularAmount * vec3(0.30);

    return
    shadowVisibility *
    attenuation *
    intensity *
    lightColor *
    (diffuse + specular);
}


void main() {
    vec4 textureColor = texture(texSampler, fragUV);

    vec3 baseColor = mix(
        textureColor.rgb,
        ubo.materialColor.rgb,
        clamp(ubo.materialColor.a, 0.0, 1.0)
    );

    if (ubo.materialColor.a > 1.5) {
        outColor = vec4(ubo.materialColor.rgb, 1.0);
        return;
    }

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(gubo.eyePos.xyz - fragWorldPos);

    vec3 ambient = 0.015 * baseColor;

    vec3 lighting = ambient;

    lighting += computePointLight(
        gubo.lightPositions[0],
        gubo.lightColors[0],
        gubo.lightEnabled.x,
        shadowMap0,
        gubo.lightViewProj[0],
        baseColor,
        N,
        V
    );

    lighting += computePointLight(
        gubo.lightPositions[1],
        gubo.lightColors[1],
        gubo.lightEnabled.y,
        shadowMap1,
        gubo.lightViewProj[1],
        baseColor,
        N,
        V
    );

    lighting += computePointLight(
        gubo.lightPositions[2],
        gubo.lightColors[2],
        gubo.lightEnabled.z,
        shadowMap2,
        gubo.lightViewProj[2],
        baseColor,
        N,
        V
    );

    lighting += computePointLight(
        gubo.lightPositions[3],
        gubo.lightColors[3],
        gubo.lightEnabled.w,
        shadowMap3,
        gubo.lightViewProj[3],
        baseColor,
        N,
        V
    );

    lighting = lighting / (lighting + vec3(1.0));
    lighting = pow(lighting, vec3(1.0 / 2.2));

    outColor = vec4(lighting, textureColor.a);
}