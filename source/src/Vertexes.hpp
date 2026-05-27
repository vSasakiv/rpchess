//
// Created by sasaki on 24/05/2026.
//

#pragma once
#include <glm/glm.hpp>

struct VertexChar {
	glm::vec3 pos;
	glm::vec3 norm;
	glm::vec2 UV;
	glm::uvec4 jointIndices;
	glm::vec4 weights;
};

struct skyBoxVertex {
	glm::vec3 pos;
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 norm;
	glm::vec2 UV;
	glm::vec4 tan;
};

