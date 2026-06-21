// THIS IS THE FILE YOU MUST START FROM!

#include <sstream>
#include <iostream>
#include <algorithm>

#include "../include/json.hpp"
#include "../include/modules/Starter.hpp"
#include "../include/modules/TextMaker.hpp"
#include "../include/modules/Scene.hpp"

// Uniform buffer for each rendered object
struct UniformBufferObject {
    alignas(16) glm::mat4 mvpMat;
    alignas(16) glm::mat4 mMat;
};

// Uniform buffer shared by the whole scene
struct GlobalUniformBufferObject {
    alignas(16) glm::vec3 lightDir;
    alignas(16) glm::vec4 lightColor;
    alignas(16) glm::vec3 eyePos;
};

// Vertex format used by the models
struct Vertex {
    glm::vec3 pos;
    glm::vec2 UV;
};

class TabletopDiceRPGArena : public BaseProject {
protected:
    // Descriptor layouts
    DescriptorSetLayout DSLlocal;
    DescriptorSetLayout DSLglobal;

    // Vertex format, render pass, and pipeline
    VertexDescriptor VD;
    RenderPass RP;
    Pipeline P;

    // Global descriptor set
    DescriptorSet DSglobal;

    // Scene and rendering references
    Scene SC;
    std::vector<VertexDescriptorRef> VDRs;
    std::vector<TechniqueRef> PRs;

    // Text rendering
    TextMaker txt;

    // Camera / projection data
    float Ar = 4.0f / 3.0f;
    glm::mat4 ViewPrj = glm::mat4(1.0f);
    glm::mat4 View = glm::mat4(1.0f);

    // Orbit camera state
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f); //the thing the camera is centered around
    float cameraYaw = glm::radians(45.0f); //yaw = horizontal rotation
    float cameraPitch = glm::radians(25.0f); // pitch = vertical angle above the table
    float cameraDistance = 6.0f; // distance = zoom distance from the table
    float cameraRotationSpeed = 1.5f;
    float cameraZoomSpeed = 4.0f;

    // -------------------------------
    // Grid and token state
    // -------------------------------

    static constexpr int GRID_ROWS = 8;
    static constexpr int GRID_COLS = 8;
    static constexpr float CELL_SIZE = 0.75f;

    // In scene.json, player_token is the third instance:
    // 0 = table_surface
    // 1 = game_board
    // 2 = player_token
    static constexpr int TOKEN_INSTANCE_INDEX = 2;

    int tokenRow = 6;
    int tokenCol = 1;

    float moveCooldown = 0.0f;

    void setWindowParameters() {
        windowWidth = 1280;
        windowHeight = 720;
        windowTitle = "Tabletop Dice RPG Arena";
        windowResizable = true;

        Ar = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    }

    void onWindowResize(int w, int h) {
        std::cout << "Window resized to: " << w << " x " << h << "\n";

        Ar = static_cast<float>(w) / static_cast<float>(h);

        RP.width = w;
        RP.height = h;

        txt.resizeScreen(w, h);
    }

    void localInit() {
        // Descriptor layout for local/object data
        DSLlocal.init(this, {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT,
             sizeof(UniformBufferObject), 1},

            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
             0, 1}
        });

        // Descriptor layout for global scene data
        DSLglobal.init(this, {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
             sizeof(GlobalUniformBufferObject), 1}
        });

        // Vertex descriptor: position + UV
        VD.init(this, {
            {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}
        }, {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos),
             sizeof(glm::vec3), POSITION},

            {0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, UV),
             sizeof(glm::vec2), UV}
        });

        // Render pass
        RP.init(this);
        RP.properties[0].clearValue = {0.0f, 0.9f, 1.0f, 1.0f};

        // Pipeline
        P.init(this,
               &VD,
               "shaders/toChangeSimplePos.vert.spv",
               "shaders/toChangeBlinnFromPos.frag.spv",
               {&DSLglobal, &DSLlocal});

        // Descriptor pool size
        DPSZs.uniformBlocksInPool = 30;
        DPSZs.texturesInPool = 30;
        DPSZs.setsInPool = 30;

        // Scene support
        VDRs.resize(1);
        VDRs[0].init("VDposUV", &VD);

        PRs.resize(1);
        PRs[0].init("BlinnPos", {
            {&P, {
                {},
                {
                    {true, 0, {}}
                }
            }}
        }, 1, &VD);

        if (SC.init(this, 1, VDRs, PRs, "assets/scenes/scene.json") != 0) {
            std::cout << "ERROR LOADING THE SCENE\n";
            exit(0);
        }

        // Text output
        txt.init(this, windowWidth, windowHeight);

        // Submit command buffer
        submitCommandBuffer("main", 0, populateCommandBufferAccess, this);

        // Initial FPS text
        txt.print(
            1.0f, 1.0f,
            "FPS:",
            1,
            "CO",
            false, false, true,
            TAL_RIGHT,
            TRH_RIGHT,
            TRV_BOTTOM,
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.8f, 0.8f, 0.0f, 1.0f}
        );
    }

    void pipelinesAndDescriptorSetsInit() {
        RP.create();

        P.create(&RP);

        DSglobal.init(this, &DSLglobal, {});

        SC.pipelinesAndDescriptorSetsInit();
        txt.pipelinesAndDescriptorSetsInit();
    }

    void pipelinesAndDescriptorSetsCleanup() {
        P.cleanup();
        RP.cleanup();
        DSglobal.cleanup();

        SC.pipelinesAndDescriptorSetsCleanup();
        txt.pipelinesAndDescriptorSetsCleanup();
    }

    void localCleanup() {
        DSLlocal.cleanup();
        DSLglobal.cleanup();

        P.destroy();
        RP.destroy();

        SC.localCleanup();
        txt.localCleanup();
    }

    static void populateCommandBufferAccess(
        VkCommandBuffer commandBuffer,
        int currentImage,
        void* params
    ) {
        auto* app = static_cast<TabletopDiceRPGArena*>(params);
        app->populateCommandBuffer(commandBuffer, currentImage);
    }

    void populateCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
        RP.begin(commandBuffer, currentImage);

        SC.populateCommandBuffer(commandBuffer, 0, currentImage);

        RP.end(commandBuffer);
    }

    void updateUniformBuffer(uint32_t currentImage) {
        // ESC closes the window
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        float deltaT = GameLogic();
        updateTokenInstance();

        // Rotating directional light
        static float lightRotationAngle = 0.0f;
        lightRotationAngle += -0.5f * deltaT;

        const glm::mat4 lightView =
            glm::rotate(glm::mat4(1.0f),
                        glm::radians(lightRotationAngle),
                        glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::rotate(glm::mat4(1.0f),
                        glm::radians(-45.0f),
                        glm::vec3(1.0f, 0.0f, 0.0f));

        const glm::vec3 lightDir =
            glm::vec3(lightView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));

        GlobalUniformBufferObject gubo{};
        gubo.lightDir = lightDir;
        gubo.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) * 5.0f;
        gubo.eyePos = glm::vec3(glm::inverse(View)[3]);

        DSglobal.map(currentImage, &gubo, 0);

        UniformBufferObject ubo{};

        for (int instanceId = 0; instanceId < SC.TI[0].InstanceCount; instanceId++) {
            ubo.mMat = SC.TI[0].I[instanceId].Wm;
            ubo.mvpMat = ViewPrj * ubo.mMat;

            SC.TI[0].I[instanceId].DS[0][0]->map(currentImage, &gubo, 0);
            SC.TI[0].I[instanceId].DS[0][1]->map(currentImage, &ubo, 0);
        }

        // FPS text
        static float elapsedT = 0.0f;
        static int countedFrames = 0;

        countedFrames++;
        elapsedT += deltaT;

        if (elapsedT > 1.0f) {
            float fps = static_cast<float>(countedFrames) / elapsedT;

            std::ostringstream oss;
            oss << "FPS: " << fps << "\n";

            txt.print(
                1.0f, 1.0f,
                oss.str(),
                1,
                "CO",
                false, false, true,
                TAL_RIGHT,
                TRH_RIGHT,
                TRV_BOTTOM,
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.8f, 0.8f, 0.0f, 1.0f}
            );

            elapsedT = 0.0f;
            countedFrames = 0;
        }

        txt.updateCommandBuffer();
    }

   float GameLogic() {
    // Camera projection parameters
    const float FOVy = glm::radians(45.0f);
    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;

    // Framework timing
    float deltaT;
    glm::vec3 m = glm::vec3(0.0f);
    glm::vec3 r = glm::vec3(0.0f);
    bool fire = false;

    getSixAxis(deltaT, m, r, fire);

    // -------------------------------
    // PLAYER MOVEMENT
    // -------------------------------

        moveCooldown -= deltaT;

        if (moveCooldown <= 0.0f) {
            if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
                tryMoveToken(-1, 0);
                moveCooldown = 0.18f;
            }

            if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) {
                tryMoveToken(1, 0);
                moveCooldown = 0.18f;
            }

            if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) {
                tryMoveToken(0, -1);
                moveCooldown = 0.18f;
            }

            if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) {
                tryMoveToken(0, 1);
                moveCooldown = 0.18f;
            }
        }

    // -------------------------------
    // ORBIT CAMERA INPUT
    // -------------------------------

    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        cameraYaw -= cameraRotationSpeed * deltaT;
    }

    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        cameraYaw += cameraRotationSpeed * deltaT;
    }

    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        cameraPitch += cameraRotationSpeed * deltaT;
    }

    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        cameraPitch -= cameraRotationSpeed * deltaT;
    }

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        cameraDistance -= cameraZoomSpeed * deltaT;
    }

    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {
        cameraDistance += cameraZoomSpeed * deltaT;
    }




    // Avoid flipping the camera upside down
    cameraPitch = std::clamp(
        cameraPitch,
        glm::radians(8.0f),
        glm::radians(80.0f)
    );

    // Avoid zooming inside the table or too far away
    cameraDistance = std::clamp(cameraDistance, 2.0f, 12.0f);

    // -------------------------------
    // CALCULATE CAMERA POSITION
    // -------------------------------

    glm::vec3 cameraPosition;

    cameraPosition.x =
        cameraTarget.x +
        cameraDistance * cos(cameraPitch) * sin(cameraYaw);

    cameraPosition.y =
        cameraTarget.y +
        cameraDistance * sin(cameraPitch);

    cameraPosition.z =
        cameraTarget.z +
        cameraDistance * cos(cameraPitch) * cos(cameraYaw);

    // -------------------------------
    // PROJECTION MATRIX
    // -------------------------------

    glm::mat4 Prj = glm::perspective(
        FOVy,
        Ar,
        nearPlane,
        farPlane
    );

    // Vulkan's clip coordinates have inverted Y compared to OpenGL
    Prj[1][1] *= -1.0f;

    // -------------------------------
    // VIEW MATRIX
    // -------------------------------

    View = glm::lookAt(
        cameraPosition,
        cameraTarget,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );



    // Final camera matrix used by the shader
    ViewPrj = Prj * View;

        if (
        glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS
        )
        {
            std::cout
            << "Yaw: " << glm::degrees(cameraYaw)
            << " Pitch: " << glm::degrees(cameraPitch)
            << " Distance: " << cameraDistance
            << std::endl;
        }
    return deltaT;
}
    //turns the board cells into a 3D position
    glm::vec3 gridToWorld(int row, int col) const {
        float x = (static_cast<float>(col) - (GRID_COLS - 1) * 0.5f) * CELL_SIZE;
        float z = (static_cast<float>(row) - (GRID_ROWS - 1) * 0.5f) * CELL_SIZE;

        return glm::vec3(x, 0.08f, z);
    }
    //check to see if it is inside the board
    bool isInsideBoard(int row, int col) const {
        return row >= 0 && row < GRID_ROWS &&
               col >= 0 && col < GRID_COLS;
    }
    //check to see if a cell is blocked, now only example for (2,3) and (4,5)
    bool isBlocked(int row, int col) const {
        return (row == 2 && col == 3) ||
               (row == 4 && col == 5);
    }


    void tryMoveToken(int dRow, int dCol) {
        int newRow = tokenRow + dRow;
        int newCol = tokenCol + dCol;

        if (!isInsideBoard(newRow, newCol)) {
            std::cout << "Blocked: outside board\n";
            return;
        }

        if (isBlocked(newRow, newCol)) {
            std::cout << "Blocked: obstacle at cell ("
                      << newRow << ", " << newCol << ")\n";
            return;
        }

        tokenRow = newRow;
        tokenCol = newCol;

        std::cout << "Token moved to cell ("
                  << tokenRow << ", " << tokenCol << ")\n";
    }

    //from object-local coordinates to world coordinates
    glm::mat4 tokenModelMatrix() const {
        glm::vec3 pos = gridToWorld(tokenRow, tokenCol);

        return glm::translate(glm::mat4(1.0f), pos) *
               glm::scale(glm::mat4(1.0f), glm::vec3(0.28f, 1.0f, 0.28f));
    }

    //Game-state objkect to the rendered scene instance
    void updateTokenInstance() {
        if (SC.TI == nullptr) {
            return;
        }

        if (SC.TI[0].InstanceCount <= TOKEN_INSTANCE_INDEX) {
            return;
        }

        SC.TI[0].I[TOKEN_INSTANCE_INDEX].Wm = tokenModelMatrix();
    }
};

int main() {
    TabletopDiceRPGArena app;

    try {
        app.run(false);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}