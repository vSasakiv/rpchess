//
// Created by vsasa on 24/05/2026.
//
#pragma once
#include "glm/gtx/quaternion.hpp"

struct Transform {
	glm::vec3 pos{0.0f, 0.0f, 0.0f};
	glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
	float scale{1.0f};
};

class PBRRenderable {
protected:
    Model M;
    DescriptorSet DS;
    Transform T;

public:
    Collider CLD;

    PBRRenderable() = default;

    PBRRenderable(BaseProject *bp, VertexDescriptor *VD, const char *meshPath, Transform transform) {
       init(bp, VD, meshPath, transform);
    }

    void init(BaseProject *bp, VertexDescriptor *VD, const char *meshPath, const Transform &transform) {
       M.init(bp, VD, meshPath, GLTF);
       T = transform;
    }

    void CollisionFitToModel() {
       CLD.fitAABB(&M);
    }

    void setColliderWorldMatrixFromTransform() {
       CLD.setWorldMatrix(glm::translate(glm::mat4(1), T.pos) * glm::scale(glm::mat4(1), glm::vec3(T.scale)));
    }

    void DescriptorSetInit(BaseProject *bp, DescriptorSetLayout *DSLlocal,
                           const std::vector<VkDescriptorImageInfo> &VaSs) {
       DS.init(bp, DSLlocal, VaSs);
    }

    void Draw(VkCommandBuffer commandBuffer, int currentImage, Pipeline &p) {
       M.bind(commandBuffer);
       DS.bind(commandBuffer, p, 1, currentImage);
       vkCmdDrawIndexed(commandBuffer,
                        static_cast<uint32_t>(M.indices.size()),
                        1, 0, 0, 0);
    }

    void MapUniformBuffer(UniformBufferObject &ubo, const glm::mat4 &viewProjection, int currentImage) {
       glm::mat4 translationMat = glm::translate(glm::mat4(1), T.pos);

       glm::mat4 rotationMat = glm::toMat4(T.rot);

       glm::mat4 scaleMat = glm::scale(glm::mat4(1), glm::vec3(T.scale));

       // do Translation -> rotation -> scaling
       ubo.mMat = translationMat * rotationMat * scaleMat;
       ubo.mvpMat = viewProjection * ubo.mMat;
       ubo.nMat = glm::inverse(glm::transpose(ubo.mMat));
       DS.map(currentImage, &ubo, 0);
    }

    void MapSolidColorBuffer(SolidColorUniformBufferObject &scubo, const glm::vec3 &colorA, const glm::vec3 &colorB,
                             int checkerBoard, const glm::vec2 &uvScale, float roughness, float metallic,
                             int currentImage) {
       scubo.colorA = colorA;
       scubo.colorB = colorB;
       scubo.checkerBoard = checkerBoard;
       scubo.uvScale = uvScale;
       scubo.roughness = roughness;
       scubo.metallic = metallic;
       DS.map(currentImage, &scubo, 1);
    }

    void DescriptorSetCleanup() {
       DS.cleanup();
    }

    void ModelCleanup() {
       M.cleanup();
    }
};