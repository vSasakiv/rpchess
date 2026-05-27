#version 450
#extension GL_ARB_separate_shader_objects : enable
layout(binding = 0, set = 0) uniform ShadowMapUniformBufferObject {
	mat4 mvpMat;
} subo;

layout(binding = 0, set = 1) uniform UniformBufferObject {
	mat4 mvpMat;
	mat4 mMat;
	mat4 nMat;
} ubo;
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNorm;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

void main() {
	gl_Position = subo.mvpMat * ubo.mMat * vec4(inPosition, 1.0);
}