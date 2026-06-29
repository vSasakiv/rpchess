#version 450
// Set 0, binding 0: global scene data.
layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 lightViewProj;
    vec4 lightPos;
    vec4 lightColor;
    vec4 eyePos;
    vec4 shadowParams;
} gubo;
// Set 1, binding 0: local per-object data.
layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 mvpMat;
    mat4 mMat;
    vec4 materialColor;
} ubo;
// Vertex input layout.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

// Values passed from vertex shader to fragment shader.
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

void main() {
    // Convert the vertex from local mesh space to world space.
    vec4 worldPos = ubo.mMat * vec4(inPosition, 1.0);

    fragWorldPos = worldPos.xyz;

    // Transform normal into world space.
    fragNormal = normalize(transpose(inverse(mat3(ubo.mMat))) * inNormal);
    fragUV = inUV;

    // Final vertex position on screen.
    gl_Position = ubo.mvpMat * vec4(inPosition, 1.0);
}