#version 450

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    vec3 lightPos; //position of the light
    vec4 lightColor; //color and intensity
    vec3 eyePos; //viewer position
} gubo;

layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 mvpMat; //Projection x View x Model
    mat4 mMat; //model matrix, Projection x view x model
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

    mat3 normalMatrix = transpose(inverse(mat3(ubo.mMat)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragUV = inUV;

    gl_Position = ubo.mvpMat * vec4(inPosition, 1.0);
}