#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform ShadowMapUniformBufferObject {
	mat4 mvpMat;
} subo;

layout(binding = 0, set = 1) uniform UniformBufferObject {
	mat4 mvpMat[65];
	mat4 mMat[65];
	mat4 nMat[65];
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNorm;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uvec4 inJointIndex;
layout(location = 4) in vec4 inJointWeight;

void main() {
	vec4 fragPos = inJointWeight.x * 
			   ubo.mMat[inJointIndex.x] * vec4(inPosition, 1.0);
	fragPos += inJointWeight.y * 
			   ubo.mMat[inJointIndex.y] * vec4(inPosition, 1.0);
	fragPos += inJointWeight.z * 
			   ubo.mMat[inJointIndex.z] * vec4(inPosition, 1.0);
	fragPos += inJointWeight.w * 
			   ubo.mMat[inJointIndex.w] * vec4(inPosition, 1.0);

	vec4 shadowPosPrj = subo.mvpMat * fragPos;
	
	gl_Position = shadowPosPrj;
}