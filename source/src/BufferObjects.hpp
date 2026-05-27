//
// Created by sasaki on 24/05/2026.
//

#pragma once

#include <glm/glm.hpp>

struct UniformBufferObjectChar {
	alignas(16) glm::mat4 mvpMat[65];
	alignas(16) glm::mat4 mMat[65];
	alignas(16) glm::mat4 nMat[65];
};

struct UniformBufferObject {
	alignas(16) glm::mat4 mvpMat;
	alignas(16) glm::mat4 mMat;
	alignas(16) glm::mat4 nMat;
};

struct SolidColorUniformBufferObject {
	alignas(16) glm::vec3 colorA;
	alignas(16) glm::vec3 colorB;
	alignas(4) float roughness;
	alignas(4) float metallic;
	alignas(8) glm::vec2 uvScale;
	alignas(4) int checkerBoard;
};

struct GlobalUniformBufferObject {
	alignas(16) glm::vec3 lightDir;
	alignas(16) glm::vec4 lightColor;
	alignas(16) glm::vec3 eyePos;
	alignas(16) glm::vec4 debugView;
};

struct skyBoxUniformBufferObject {
	alignas(16) glm::mat4 mvpMat;
};

struct ShadowMapUniformBufferObject {
	alignas(16) glm::mat4 mvpMat;
};

