#version 450

layout(set = 0, binding = 0) uniform ShadowGlobalUniformBufferObject {
    mat4 lightViewProj;
} sgubo;

layout(set = 1, binding = 0) uniform ShadowLocalUniformBufferObject {
    mat4 mMat;
} slubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

void main() {
    gl_Position = sgubo.lightViewProj * slubo.mMat * vec4(inPosition, 1.0);
}