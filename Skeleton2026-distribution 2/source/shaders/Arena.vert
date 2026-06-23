#version 450

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 lightViewProj;
    vec4 lightPos;
    vec4 lightColor;
    vec4 eyePos;
    vec4 shadowParams;
} gubo;

layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 mvpMat;
    mat4 mMat;
    vec4 materialColor;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

void main() {
    vec4 worldPos = ubo.mMat * vec4(inPosition, 1.0);

    fragWorldPos = worldPos.xyz;
    fragNormal = normalize(transpose(inverse(mat3(ubo.mMat))) * inNormal);
    fragUV = inUV;

    gl_Position = ubo.mvpMat * vec4(inPosition, 1.0);
}