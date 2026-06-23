#include <sstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <cmath>

#include "../include/json.hpp"
#include "../include/modules/Starter.hpp"
#include "../include/modules/TextMaker.hpp"
#include "../include/modules/Scene.hpp"


// Uniform buffer for each rendered object
struct UniformBufferObject {
    alignas(16) glm::mat4 mvpMat;
    alignas(16) glm::mat4 mMat;
    alignas(16) glm::vec4 materialColor;
};


// Uniform buffer shared by the whole scene
struct GlobalUniformBufferObject {
    alignas(16) glm::mat4 lightViewProj;
    alignas(16) glm::vec4 lightPos;
    alignas(16) glm::vec4 lightColor;
    alignas(16) glm::vec4 eyePos;
    alignas(16) glm::vec4 shadowParams;
};

struct ShadowGlobalUniformBufferObject {
    alignas(16) glm::mat4 lightViewProj;
};

struct ShadowLocalUniformBufferObject {
    alignas(16) glm::mat4 mMat;
};

// Vertex format used by our Arena shaders.
// Each vertex has a position, a normal, and texture coordinates.
struct Vertex {
    glm::vec3 pos;
    glm::vec3 norm;
    glm::vec2 UV;
};


class TabletopDiceRPGArena : public BaseProject {
protected:
    // -------------------------------
    // Vulkan / framework objects
    // -------------------------------

    DescriptorSetLayout DSLlocal;
    DescriptorSetLayout DSLglobal;

    DescriptorSetLayout DSLshadowGlobal;
    DescriptorSetLayout DSLshadowLocal;

    VertexDescriptor VD;

    RenderPass RP;
    RenderPass RPshadow;

    Pipeline P;
    Pipeline Pshadow;

    Scene SC;
    std::vector<VertexDescriptorRef> VDRs;
    std::vector<TechniqueRef> PRs;

    TextMaker txt;


    // -------------------------------
    // Camera state
    // -------------------------------

    float Ar = 4.0f / 3.0f;

    glm::mat4 ViewPrj = glm::mat4(1.0f);
    glm::mat4 View = glm::mat4(1.0f);

    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);

    float cameraYaw = glm::radians(45.0f);
    float cameraPitch = glm::radians(40.0f);
    float cameraDistance = 8.0f;

    float cameraRotationSpeed = 1.5f;
    float cameraZoomSpeed = 4.0f;


    // -------------------------------
    // Grid and token state
    // -------------------------------

    static constexpr int GRID_ROWS = 8;
    static constexpr int GRID_COLS = 8;
    static constexpr float CELL_SIZE = 0.75f;

    // Scene instance indices from scene.json:
    // 0 = table_surface
    // 1 = game_board
    // 2 = player_token
    // 3 = blocked_cell_a
    // 4 = blocked_cell_b
    // 5 = die_1
    // 6 = die_2
    static constexpr int TOKEN_INSTANCE_INDEX = 2;
    static constexpr int DIE_1_INSTANCE_INDEX = 5;
    static constexpr int DIE_2_INSTANCE_INDEX = 6;

    int tokenRow = 6;
    int tokenCol = 1;

    float moveCooldown = 0.0f;


    // -------------------------------
    // Dice and movement-point state
    // -------------------------------

    static constexpr int DICE_MIN = 1;
    static constexpr int DICE_MAX = 6;

    int die1Value = 1;
    int die2Value = 1;

    float die1Spin = 0.0f;
    float die2Spin = 0.0f;

    int movementPoints = 0;

    // Where the dice start when thrown
    glm::vec3 die1StartPosition = glm::vec3(-3.2f, 0.35f, -2.6f);
    glm::vec3 die2StartPosition = glm::vec3(-2.5f, 0.35f, -2.6f);

    glm::vec3 die1LandPosition = glm::vec3(-2.8f, 0.35f, -2.2f);
    glm::vec3 die2LandPosition = glm::vec3(-2.2f, 0.35f, -2.2f);

    bool diceRolling = false;
    float diceRollTimer = 0.0f;
    float diceRollDuration = 1.35f;

    float rollCooldown = 0.0f;

    std::mt19937 randomEngine{std::random_device{}()};
    std::uniform_int_distribution<int> diceDistribution{DICE_MIN, DICE_MAX};


    // -------------------------------
    // Window setup
    // -------------------------------

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


    // -------------------------------
    // Vulkan local initialization
    // -------------------------------

    void localInit() {
        // Local descriptor layout:
        // binding 0 = per-object uniform buffer
        // binding 1 = object texture
        DSLlocal.init(this, {
    {
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_SHADER_STAGE_ALL_GRAPHICS,
        sizeof(UniformBufferObject),
        1
    },
    {
        1,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        1
    }
});

        DSLglobal.init(this, {
            {
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_ALL_GRAPHICS,
                sizeof(GlobalUniformBufferObject),
                1
            },
            {
                1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                1
            }
        });

        DSLshadowGlobal.init(this, {
            {
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT,
                sizeof(ShadowGlobalUniformBufferObject),
                1
            }
        });

        DSLshadowLocal.init(this, {
            {
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT,
                sizeof(ShadowLocalUniformBufferObject),
                1
            }
        });

        // Vertex descriptor:
        // location 0 = position
        // location 1 = normal
        // location 2 = UV
        VD.init(this, {
            {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}
        }, {
            {
                0,
                0,
                VK_FORMAT_R32G32B32_SFLOAT,
                offsetof(Vertex, pos),
                sizeof(glm::vec3),
                POSITION
            },
            {
                0,
                1,
                VK_FORMAT_R32G32B32_SFLOAT,
                offsetof(Vertex, norm),
                sizeof(glm::vec3),
                NORMAL
            },
            {
                0,
                2,
                VK_FORMAT_R32G32_SFLOAT,
                offsetof(Vertex, UV),
                sizeof(glm::vec2),
                UV
            }
        });

        // Main render pass to the screen.
        RP.init(this);
        RP.properties[0].clearValue = {0.05f, 0.07f, 0.10f, 1.0f};

        // Shadow render pass: depth-only texture, 2048 x 2048.
        RPshadow.init(
            this,
            2048,
            2048,
            -1,
            RenderPass::getStandardAttchmentsProperties(AT_DEPTH_ONLY, this),
            RenderPass::getStandardDependencies(ATDEP_DEPTH_TRANS),
            true
        );

        // Shadow pipeline: writes only depth from the lamp view.
        Pshadow.init(
            this,
            &VD,
            "shaders/Shadow.vert.spv",
            "shaders/Shadow.frag.spv",
            {&DSLshadowGlobal, &DSLshadowLocal}
        );

        Pshadow.setCullMode(VK_CULL_MODE_NONE);
        Pshadow.setCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

        // Main arena pipeline.
        P.init(
            this,
            &VD,
            "shaders/Arena.vert.spv",
            "shaders/Arena.frag.spv",
            {&DSLglobal, &DSLlocal}
        );

        // Disabled for now because our custom cube may have mixed winding.
        P.setCullMode(VK_CULL_MODE_NONE);
        P.setCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);
        // Descriptor pool size.
        // We have several scene objects, so this must be larger than the starter default.
        DPSZs.uniformBlocksInPool = 30;
        DPSZs.texturesInPool = 30;
        DPSZs.setsInPool = 30;

        // Scene support names.
        // These must match scene.json.
        VDRs.resize(1);
        VDRs[0].init("VDposNormUV", &VD);


        TextureDefs shadowMapTexture{};
        shadowMapTexture.fromInstance = false;
        shadowMapTexture.pos = 0;
        shadowMapTexture.info = {};

        TextureDefs objectTexture{};
        objectTexture.fromInstance = true;
        objectTexture.pos = 0;
        objectTexture.info = {};
        PRs.resize(1);
        PRs[0].init(
            "ArenaTechnique",
            {
                // Pass 0: shadow pass.
                // Descriptor set 0 = shadow global UBO.
                // Descriptor set 1 = shadow local UBO.
                {
                    &Pshadow,
                    {
                        {},
                        {}
                    }
                },

                // Pass 1: main render pass.
                // Descriptor set 0 = global UBO + shadow map.
                // Descriptor set 1 = local UBO + object texture.
                {
                    &P,
                    {
                        {shadowMapTexture},
                        {objectTexture}
                    }
                }
            },
            1,
            &VD
        );

        if (SC.init(this, 2, VDRs, PRs, "assets/scenes/scene.json") != 0) {
            std::cout << "ERROR LOADING THE SCENE\n";
            exit(0);
        }

        txt.init(this, windowWidth, windowHeight);

        submitCommandBuffer("main", 0, populateCommandBufferAccess, this);

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
        RPshadow.create();
        RP.create();

        Pshadow.create(&RPshadow);
        P.create(&RP);

        // Now that RPshadow.create() has created the depth texture and sampler,
        // update the placeholder shadow-map descriptor used by the main pass.
        PRs[0].PT[1].texDefs[0][0].info =
            RPshadow.attachments[0].getViewAndSampler();

        SC.pipelinesAndDescriptorSetsInit();
        txt.pipelinesAndDescriptorSetsInit();
    }

    void pipelinesAndDescriptorSetsCleanup() {
        Pshadow.cleanup();
        P.cleanup();

        RPshadow.cleanup();
        RP.cleanup();

        SC.pipelinesAndDescriptorSetsCleanup();
        txt.pipelinesAndDescriptorSetsCleanup();
    }
    void localCleanup() {
        DSLlocal.cleanup();
        DSLglobal.cleanup();
        DSLshadowGlobal.cleanup();
        DSLshadowLocal.cleanup();

        Pshadow.destroy();
        P.destroy();

        RPshadow.destroy();
        RP.destroy();

        SC.localCleanup();
        txt.localCleanup();
    }


    // -------------------------------
    // Command buffer
    // -------------------------------

    static void populateCommandBufferAccess(
        VkCommandBuffer commandBuffer,
        int currentImage,
        void* params
    ) {
        auto* app = static_cast<TabletopDiceRPGArena*>(params);
        app->populateCommandBuffer(commandBuffer, currentImage);
    }


    void populateCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
        // Pass 0: render scene from lamp view into the shadow depth texture.
        RPshadow.begin(commandBuffer, currentImage);
        SC.populateCommandBuffer(commandBuffer, 0, currentImage);
        RPshadow.end(commandBuffer);

        // Pass 1: render scene normally from camera view, sampling the shadow map.
        RP.begin(commandBuffer, currentImage);
        SC.populateCommandBuffer(commandBuffer, 1, currentImage);
        RP.end(commandBuffer);
    }


    // -------------------------------
    // Per-frame update
    // -------------------------------

    void updateUniformBuffer(uint32_t currentImage) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        float deltaT = GameLogic();

        updateTokenInstance();
        updateDiceInstances(deltaT);

        glm::mat4 lightViewProj = computeLightViewProj();

        ShadowGlobalUniformBufferObject sgubo{};
        sgubo.lightViewProj = lightViewProj;

        GlobalUniformBufferObject gubo{};
        gubo.lightViewProj = lightViewProj;
        gubo.lightPos = glm::vec4(lampPosition(), 1.0f);
        gubo.lightColor = glm::vec4(1.0f, 0.82f, 0.55f, 1.0f) * 7.0f;
        gubo.eyePos = glm::vec4(glm::vec3(glm::inverse(View)[3]), 1.0f);

        // x = base bias
        // y = shadow strength
        // z = shadow map size
        // w = unused
        gubo.shadowParams = glm::vec4(0.004f, 0.65f, 2048.0f, 0.0f);

        UniformBufferObject ubo{};
        ShadowLocalUniformBufferObject slubo{};

        for (int instanceId = 0; instanceId < SC.TI[0].InstanceCount; instanceId++) {
            glm::mat4 model = SC.TI[0].I[instanceId].Wm;

            // Pass 0: shadow map uniforms.
            slubo.mMat = model;

            SC.TI[0].I[instanceId].DS[0][0]->map(currentImage, &sgubo, 0);
            SC.TI[0].I[instanceId].DS[0][1]->map(currentImage, &slubo, 0);

            // Pass 1: main render uniforms.
            ubo.mMat = model;
            ubo.mvpMat = ViewPrj * model;
            ubo.materialColor = objectMaterialColor(instanceId);

            SC.TI[0].I[instanceId].DS[1][0]->map(currentImage, &gubo, 0);
            SC.TI[0].I[instanceId].DS[1][1]->map(currentImage, &ubo, 0);
        }

        updateHudText(deltaT);

        txt.updateCommandBuffer();
    }


    void updateHudText(float deltaT) {
        static float elapsedT = 0.0f;
        static int countedFrames = 0;

        countedFrames++;
        elapsedT += deltaT;

        if (elapsedT > 1.0f) {
            float fps = static_cast<float>(countedFrames) / elapsedT;

            std::ostringstream oss;
            oss << "FPS: " << fps << "\n";
            oss << "Dice: " << die1Value << " + " << die2Value << "\n";
            oss << "Move points: " << movementPoints << "\n";

            if (diceRolling) {
                oss << "Rolling...\n";
            } else {
                oss << "SPACE: roll dice\n";
            }

            oss << "I/J/K/L: move token\n";

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
    }


    // -------------------------------
    // Dice logic
    // -------------------------------

    void startDiceRoll() {
        if (diceRolling) {
            return;
        }

        diceRolling = true;
        diceRollTimer = 0.0f;

        // While rolling, old movement points should not be usable.
        movementPoints = 0;

        // Reset spin at the start of each throw.
        die1Spin = 0.0f;
        die2Spin = 0.0f;

        // Small random variation in landing positions.
        float offset1X = (diceDistribution(randomEngine) - 3.5f) * 0.08f;
        float offset1Z = (diceDistribution(randomEngine) - 3.5f) * 0.08f;

        float offset2X = (diceDistribution(randomEngine) - 3.5f) * 0.08f;
        float offset2Z = (diceDistribution(randomEngine) - 3.5f) * 0.08f;

        die1LandPosition = glm::vec3(-2.8f + offset1X, 0.35f, -2.2f + offset1Z);
        die2LandPosition = glm::vec3(-2.2f + offset2X, 0.35f, -2.2f + offset2Z);

        std::cout << "Throwing dice...\n";
    }


    void updateDice(float deltaT) {
        if (!diceRolling) {
            return;
        }

        diceRollTimer += deltaT;

        // Change values while rolling to show activity.
        die1Value = diceDistribution(randomEngine);
        die2Value = diceDistribution(randomEngine);

        if (diceRollTimer >= diceRollDuration) {
            diceRolling = false;

            die1Value = diceDistribution(randomEngine);
            die2Value = diceDistribution(randomEngine);

            movementPoints = die1Value + die2Value;

            std::cout
                << "Dice result: "
                << die1Value << " + " << die2Value
                << " = " << movementPoints
                << " movement points\n";
        }
    }


    // -------------------------------
    // Main game/camera logic
    // -------------------------------

    float GameLogic() {
        const float FOVy = glm::radians(45.0f);
        const float nearPlane = 0.1f;
        const float farPlane = 100.0f;

        float deltaT;
        glm::vec3 m = glm::vec3(0.0f);
        glm::vec3 r = glm::vec3(0.0f);
        bool fire = false;

        getSixAxis(deltaT, m, r, fire);

        // -------------------------------
        // Player token movement
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
        // Dice roll input
        // -------------------------------

        rollCooldown -= deltaT;

        if (
            rollCooldown <= 0.0f &&
            glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS
        ) {
            startDiceRoll();
            rollCooldown = 0.4f;
        }

        updateDice(deltaT);

        // -------------------------------
        // Orbit camera input
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

        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            cameraDistance += cameraZoomSpeed * deltaT;
        }

        cameraPitch = std::clamp(
            cameraPitch,
            glm::radians(8.0f),
            glm::radians(80.0f)
        );

        cameraDistance = std::clamp(cameraDistance, 2.0f, 12.0f);

        glm::vec3 cameraPosition;

        cameraPosition.x =
            cameraTarget.x +
            cameraDistance * std::cos(cameraPitch) * std::sin(cameraYaw);

        cameraPosition.y =
            cameraTarget.y +
            cameraDistance * std::sin(cameraPitch);

        cameraPosition.z =
            cameraTarget.z +
            cameraDistance * std::cos(cameraPitch) * std::cos(cameraYaw);

        glm::mat4 Prj = glm::perspective(
            FOVy,
            Ar,
            nearPlane,
            farPlane
        );

        // Vulkan clip coordinates use inverted Y compared to OpenGL.
        Prj[1][1] *= -1.0f;

        View = glm::lookAt(
            cameraPosition,
            cameraTarget,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        // Projection-view matrix used before multiplying by each model matrix.
        ViewPrj = Prj * View;

        return deltaT;
    }


    // -------------------------------
    // Grid/token logic
    // -------------------------------

    glm::vec3 gridToWorld(int row, int col) const {
        float x =
            (static_cast<float>(col) - (GRID_COLS - 1) * 0.5f) * CELL_SIZE;

        float z =
            (static_cast<float>(row) - (GRID_ROWS - 1) * 0.5f) * CELL_SIZE;

        return glm::vec3(x, 0.38f, z);
    }


    bool isInsideBoard(int row, int col) const {
        return row >= 0 && row < GRID_ROWS &&
               col >= 0 && col < GRID_COLS;
    }


    bool isBlocked(int row, int col) const {
        return (row == 2 && col == 3) ||
               (row == 4 && col == 5);
    }


    void tryMoveToken(int dRow, int dCol) {
        if (movementPoints <= 0) {
            std::cout << "No movement points. Roll dice with SPACE first.\n";
            return;
        }

        int newRow = tokenRow + dRow;
        int newCol = tokenCol + dCol;

        if (!isInsideBoard(newRow, newCol)) {
            std::cout << "Blocked: outside board\n";
            return;
        }

        if (isBlocked(newRow, newCol)) {
            std::cout
                << "Blocked: obstacle at cell ("
                << newRow << ", " << newCol << ")\n";
            return;
        }

        tokenRow = newRow;
        tokenCol = newCol;

        movementPoints--;

        std::cout
            << "Token moved to cell ("
            << tokenRow << ", " << tokenCol << ")\n"
            << "Movement points left: "
            << movementPoints << "\n";
    }


    // -------------------------------
    // Model matrices
    // -------------------------------

    glm::mat4 tokenModelMatrix() const {
        glm::vec3 pos = gridToWorld(tokenRow, tokenCol);

        return glm::translate(glm::mat4(1.0f), pos) *
               glm::scale(glm::mat4(1.0f), glm::vec3(0.55f, 0.35f, 0.55f));
    }


    glm::mat4 diceModelMatrix(
       const glm::vec3& position,
       float spin,
       int value,
       bool secondDie
   ) const {
        float valueAngle = glm::radians(static_cast<float>(value) * 25.0f);

        glm::mat4 M = glm::mat4(1.0f);

        // Translation: where the die is in the world.
        M = glm::translate(M, position);

        // Rotation: while rolling, spin strongly around several axes.
        // When stopped, valueAngle makes different values rest differently.
        if (secondDie) {
            M = glm::rotate(M, -spin + valueAngle, glm::vec3(1.0f, 0.0f, 0.0f));
            M = glm::rotate(M, spin * 0.9f + valueAngle, glm::vec3(0.0f, 1.0f, 0.0f));
            M = glm::rotate(M, spin * 0.6f, glm::vec3(0.0f, 0.0f, 1.0f));
        } else {
            M = glm::rotate(M, spin + valueAngle, glm::vec3(1.0f, 0.0f, 0.0f));
            M = glm::rotate(M, spin * 1.1f + valueAngle, glm::vec3(0.0f, 1.0f, 0.0f));
            M = glm::rotate(M, -spin * 0.7f, glm::vec3(0.0f, 0.0f, 1.0f));
        }

        // Dice size.
        M = glm::scale(M, glm::vec3(0.45f, 0.45f, 0.45f));

        return M;
    }


    glm::vec3 lampPosition() const {
        return glm::vec3(3.0f, 2.2f, 2.2f);
    }

    glm::vec3 lampTarget() const {
        return glm::vec3(0.0f, 0.15f, 0.0f);
    }

    glm::mat4 computeLightViewProj() const {
        glm::mat4 lightView = glm::lookAt(
            lampPosition(),
            lampTarget(),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        glm::mat4 lightProj = glm::perspective(
            glm::radians(70.0f),
            1.0f,
            0.1f,
            12.0f
        );

        // Vulkan clip-space correction.
        lightProj[1][1] *= -1.0f;

        return lightProj * lightView;
    }

    glm::vec4 objectMaterialColor(int instanceId) const {
        switch (instanceId) {
        case 0:
            // table_surface
            return glm::vec4(0.45f, 0.25f, 0.10f, 1.0f);

        case 1:
            // game_board
            return glm::vec4(0.15f, 0.45f, 0.18f, 1.0f);

        case 2:
            // player_token
            return glm::vec4(1.0f, 0.25f, 0.10f, 1.0f);

        case 3:
        case 4:
            // blocked cells / obstacles
            return glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);

        case 5:
        case 6:
            // dice
            return glm::vec4(0.95f, 0.95f, 0.90f, 1.0f);

        default:
            return glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    void updateDiceInstances(float deltaT) {
        if (SC.TI == nullptr) {
            return;
        }

        if (SC.TI[0].InstanceCount <= DIE_2_INSTANCE_INDEX) {
            return;
        }

        glm::vec3 die1Position = die1LandPosition;
        glm::vec3 die2Position = die2LandPosition;

        if (diceRolling) {
            // Progress goes from 0 to 1 during the roll.
            float t = diceRollTimer / diceRollDuration;
            t = std::clamp(t, 0.0f, 1.0f);

            // Smooth horizontal movement.
            float smoothT = t * t * (3.0f - 2.0f * t);

            // Manual PI constant to avoid relying on extra includes.
            const float PI = 3.14159265359f;

            // Main throw arc: starts low, goes high, lands low.
            float arcHeight = std::sin(PI * t) * 0.75f;

            // Small bounce near the end.
            float bounce = 0.0f;
            if (t > 0.65f) {
                float bounceT = (t - 0.65f) / 0.35f;
                bounce = std::abs(std::sin(bounceT * PI * 3.0f)) *
                         (1.0f - bounceT) *
                         0.18f;
            }

            die1Position =
                die1StartPosition * (1.0f - smoothT) +
                die1LandPosition * smoothT;

            die2Position =
                die2StartPosition * (1.0f - smoothT) +
                die2LandPosition * smoothT;

            die1Position.y += arcHeight + bounce;
            die2Position.y += arcHeight + bounce * 0.8f;

            // Fast spin while the dice are rolling.
            die1Spin += 16.0f * deltaT;
            die2Spin += 19.0f * deltaT;
        }

        SC.TI[0].I[DIE_1_INSTANCE_INDEX].Wm =
            diceModelMatrix(die1Position, die1Spin, die1Value, false);

        SC.TI[0].I[DIE_2_INSTANCE_INDEX].Wm =
            diceModelMatrix(die2Position, die2Spin, die2Value, true);
    }


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