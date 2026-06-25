#include <sstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <cmath>

#include "../include/json.hpp"
#include "../include/modules/Starter.hpp"
#include <glm/ext/matrix_clip_space.hpp>
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

    // Four point lights.
    // xyz = world position, w = unused
    alignas(16) glm::vec4 lightPositions[4];

    // rgb = color, w = intensity
    alignas(16) glm::vec4 lightColors[4];

    // x/y/z/w = enabled state for light 0/1/2/3
    alignas(16) glm::vec4 lightEnabled;

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
    static constexpr float CELL_SIZE = 1.0f;

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
    static constexpr int LAMP_1_POST_INSTANCE_INDEX = 7;
    static constexpr int LAMP_1_BULB_INSTANCE_INDEX = 8;

    static constexpr int LAMP_2_POST_INSTANCE_INDEX = 21;
    static constexpr int LAMP_2_BULB_INSTANCE_INDEX = 22;

    static constexpr int LAMP_3_POST_INSTANCE_INDEX = 23;
    static constexpr int LAMP_3_BULB_INSTANCE_INDEX = 24;

    static constexpr int LAMP_4_POST_INSTANCE_INDEX = 25;
    static constexpr int LAMP_4_BULB_INSTANCE_INDEX = 26;
    static constexpr int DICE_TRAY_FLOOR_INSTANCE_INDEX = 27;
    static constexpr int DICE_TRAY_WALL_LEFT_INSTANCE_INDEX = 28;
    static constexpr int DICE_TRAY_WALL_RIGHT_INSTANCE_INDEX = 29;
    static constexpr int DICE_TRAY_WALL_BACK_INSTANCE_INDEX = 30;
    static constexpr int DICE_TRAY_WALL_FRONT_INSTANCE_INDEX = 31;

    int tokenRow = 6;
    int tokenCol = 1;

    float moveCooldown = 0.0f;

    // -------------------------------
    // Four corner lights
    // -------------------------------

    static constexpr int NUM_CORNER_LIGHTS = 4;

    // The lights are placed around the board.
    // These are point lights used by the fragment shader.
    glm::vec4 cornerLightPositions[NUM_CORNER_LIGHTS] = {
        glm::vec4(-4.2f, 2.4f, -4.2f, 1.0f), // light 1: back-left
        glm::vec4( 4.2f, 2.4f, -4.2f, 1.0f), // light 2: back-right
        glm::vec4(-4.2f, 2.4f,  4.2f, 1.0f), // light 3: front-left
        glm::vec4( 4.2f, 2.4f,  4.2f, 1.0f)  // light 4: front-right
    };

    // rgb = color, w = intensity.
    // Lower intensity than the old single lamp, because now several lights can be active.
    glm::vec4 cornerLightColors[NUM_CORNER_LIGHTS] = {
        glm::vec4(1.0f, 0.82f, 0.55f, 3.2f),
        glm::vec4(1.0f, 0.82f, 0.55f, 3.2f),
        glm::vec4(1.0f, 0.82f, 0.55f, 3.2f),
        glm::vec4(1.0f, 0.82f, 0.55f, 3.2f)
    };

    bool cornerLightEnabled[NUM_CORNER_LIGHTS] = {
        true,
        true,
        true,
        true
    };

    float lightToggleCooldown[NUM_CORNER_LIGHTS] = {
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };

    // -------------------------------
    // Dice and movement-point state
    // -------------------------------

    static constexpr int DICE_MIN = 1;
    static constexpr int DICE_MAX = 6;

    int die1Value = 1;
    int die2Value = 1;

    int movementPoints = 0;

    // Board/token height is around y = 0.38 in this scene.
    // The dice collision uses the center of the die, so the center must be ABOVE the board.
    // We use a slightly conservative height because the die rotates and its corners need clearance.
    static constexpr float DICE_BOARD_Y = 0.38f;
    static constexpr float DICE_REST_Y = 0.48f;

    static constexpr float DICE_GRAVITY = -8.8f;

    // Higher bounce factor = the dice keep more energy after impact.
    // This makes the roll last longer.
    static constexpr float DICE_BOUNCE_FACTOR = 0.58f;

    // Higher value means less energy loss when sliding on the board.
    static constexpr float DICE_FRICTION = 0.88f;

    // Air drag close to 1.0 means slower loss of horizontal velocity.
    static constexpr float DICE_AIR_DRAG = 0.993f;

    // Higher value means spin dies more slowly.
    static constexpr float DICE_ANGULAR_DAMPING = 0.84f;

    // Approximate collision radius for each die.
    // The dice are rendered as cubes, but collision is simplified as circle collision in XZ.
    static constexpr float DICE_COLLISION_RADIUS = 0.42f;

    // Bounciness when the two dice collide with each other.
    static constexpr float DICE_DICE_RESTITUTION = 0.65f;

    // Dice movement bounds on/near the board.// Dice tray area.
    // The dice now roll inside the box next to the board, not across the board.
    static constexpr float DICE_TRAY_CENTER_X = 5.2f;
    static constexpr float DICE_TRAY_CENTER_Z = 0.0f;

    static constexpr float DICE_TRAY_HALF_X = 1.05f;
    static constexpr float DICE_TRAY_HALF_Z = 0.95f;

    // Small margin so the dice do not visually pass through the tray walls.
    static constexpr float DICE_TRAY_DICE_MARGIN = 0.32f;

    static constexpr float DICE_MIN_X =
        DICE_TRAY_CENTER_X - DICE_TRAY_HALF_X + DICE_TRAY_DICE_MARGIN;

    static constexpr float DICE_MAX_X =
        DICE_TRAY_CENTER_X + DICE_TRAY_HALF_X - DICE_TRAY_DICE_MARGIN;

    static constexpr float DICE_MIN_Z =
        DICE_TRAY_CENTER_Z - DICE_TRAY_HALF_Z + DICE_TRAY_DICE_MARGIN;

    static constexpr float DICE_MAX_Z =
        DICE_TRAY_CENTER_Z + DICE_TRAY_HALF_Z - DICE_TRAY_DICE_MARGIN;

    glm::vec3 die1Position = glm::vec3(DICE_TRAY_CENTER_X - 0.35f, DICE_REST_Y, DICE_TRAY_CENTER_Z - 0.20f);
    glm::vec3 die2Position = glm::vec3(DICE_TRAY_CENTER_X + 0.35f, DICE_REST_Y, DICE_TRAY_CENTER_Z + 0.20f);

    glm::vec3 die1Velocity = glm::vec3(0.0f);
    glm::vec3 die2Velocity = glm::vec3(0.0f);

    glm::vec3 die1Rotation = glm::vec3(0.0f);
    glm::vec3 die2Rotation = glm::vec3(0.0f);

    glm::vec3 die1AngularVelocity = glm::vec3(0.0f);
    glm::vec3 die2AngularVelocity = glm::vec3(0.0f);

    bool die1Sleeping = true;
    bool die2Sleeping = true;

    bool diceRolling = false;
    float diceRollTimer = 0.0f;

    // Safety timeout so the dice never roll forever.
    float diceRollMaxDuration = 12.0f;

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

        DPSZs.uniformBlocksInPool = 80;
        DPSZs.texturesInPool = 80;
        DPSZs.setsInPool = 80;

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
    // Pass 0:
    // Render only real shadow casters from the lamp point of view.
    // Do NOT render the table, board, or lamp into the shadow map.
    // They should receive shadows, not create giant fake/self shadows.
    RPshadow.begin(commandBuffer, currentImage);
    populateShadowCommandBuffer(commandBuffer, currentImage);
    RPshadow.end(commandBuffer);

    // Pass 1:
    // Render the normal camera view and sample the shadow map.
    RP.begin(commandBuffer, currentImage);
    SC.populateCommandBuffer(commandBuffer, 1, currentImage);
    RP.end(commandBuffer);
}
    bool isLampInstance(int instanceId) const {
        return
            instanceId == LAMP_1_POST_INSTANCE_INDEX ||
            instanceId == LAMP_1_BULB_INSTANCE_INDEX ||
            instanceId == LAMP_2_POST_INSTANCE_INDEX ||
            instanceId == LAMP_2_BULB_INSTANCE_INDEX ||
            instanceId == LAMP_3_POST_INSTANCE_INDEX ||
            instanceId == LAMP_3_BULB_INSTANCE_INDEX ||
            instanceId == LAMP_4_POST_INSTANCE_INDEX ||
            instanceId == LAMP_4_BULB_INSTANCE_INDEX;
    }

bool instanceCastsShadow(int instanceId) const {
    // Scene instance indices from scene.json:
    // 0 = table_surface
    // 1 = game_board
    // 7 = lamp_post
    // 8 = lamp_bulb
    //
    // These should not be rendered into the shadow map.
    // The board and table are receivers.
    // The lamp should not cast a weird lamp-shaped shadow on the board.
        if (
            instanceId == 0 ||
            instanceId == 1 ||
            instanceId == DICE_TRAY_FLOOR_INSTANCE_INDEX ||
            isLampInstance(instanceId)
        ) {
            return false;
        }

    return true;
}


void populateShadowCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
    constexpr int passId = 0;

    for (int techniqueId = 0; techniqueId < SC.TechniqueInstanceCount; techniqueId++) {
        Pipeline* pipeline = SC.TI[techniqueId].T->PT[passId].P;

        if (pipeline == nullptr) {
            continue;
        }

        pipeline->bind(commandBuffer);

        for (int instanceId = 0; instanceId < SC.TI[techniqueId].InstanceCount; instanceId++) {
            if (!instanceCastsShadow(instanceId)) {
                continue;
            }

            Instance& instance = SC.TI[techniqueId].I[instanceId];

            SC.M[instance.Mid]->bind(commandBuffer);

            for (int setId = 0; setId < instance.NDs[passId]; setId++) {
                instance.DS[passId][setId]->bind(commandBuffer, *pipeline, setId, currentImage);
            }

            vkCmdDrawIndexed(
                commandBuffer,
                static_cast<uint32_t>(SC.M[instance.Mid]->indices.size()),
                1,
                0,
                0,
                0
            );
        }
    }
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
        updateLampInstances();

        glm::mat4 lightViewProj = computeLightViewProj();

        ShadowGlobalUniformBufferObject sgubo{};
        sgubo.lightViewProj = lightViewProj;

        GlobalUniformBufferObject gubo{};
        gubo.lightViewProj = lightViewProj;

        for (int i = 0; i < NUM_CORNER_LIGHTS; i++) {
            gubo.lightPositions[i] = cornerLightPositions[i];
            gubo.lightColors[i] = cornerLightColors[i];
        }

        gubo.lightEnabled = glm::vec4(
            cornerLightEnabled[0] ? 1.0f : 0.0f,
            cornerLightEnabled[1] ? 1.0f : 0.0f,
            cornerLightEnabled[2] ? 1.0f : 0.0f,
            cornerLightEnabled[3] ? 1.0f : 0.0f
        );

        gubo.eyePos = glm::vec4(glm::vec3(glm::inverse(View)[3]), 1.0f);
        // x = base bias
        // y = shadow strength
        // z = shadow map size
        // w = unused
        gubo.shadowParams = glm::vec4(0.0065f, 0.72f, 0.025f, 1.25f);
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
            oss << "1/2/3/4: toggle lights\n";

            oss << "Lights: ";
            for (int i = 0; i < NUM_CORNER_LIGHTS; i++) {
                oss << (cornerLightEnabled[i] ? "ON" : "OFF");

                if (i < NUM_CORNER_LIGHTS - 1) {
                    oss << " | ";
                }
            }
            oss << "\n";

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

    float randomFloat(float minValue, float maxValue) {
        std::uniform_real_distribution<float> distribution(minValue, maxValue);
        return distribution(randomEngine);
    }

    void startDiceRoll() {
        if (diceRolling) {
            return;
        }

        diceRolling = true;
        diceRollTimer = 0.0f;

        // Old movement points are removed while rolling.
        movementPoints = 0;

        die1Sleeping = false;
        die2Sleeping = false;

        // Start positions, slightly above the board.
        die1Position = glm::vec3(
            DICE_TRAY_CENTER_X - 0.35f,
            DICE_REST_Y + 0.55f,
            DICE_TRAY_CENTER_Z - 0.20f
        );

        die2Position = glm::vec3(
            DICE_TRAY_CENTER_X + 0.35f,
            DICE_REST_Y + 0.55f,
            DICE_TRAY_CENTER_Z + 0.20f
        );

        // Initial velocities.
        // x/z move them across the board, y throws them upward.
        // Smaller horizontal velocities because the dice now roll inside a tray.
        die1Velocity = glm::vec3(
            randomFloat(-0.9f, 0.9f),
            randomFloat(3.5f, 4.4f),
            randomFloat(-0.8f, 0.8f)
        );

        die2Velocity = glm::vec3(
            randomFloat(-0.9f, 0.9f),
            randomFloat(3.4f, 4.3f),
            randomFloat(-0.8f, 0.8f)
        );

        // Strong random angular velocity makes the dice spin while flying.
        die1AngularVelocity = glm::vec3(
            randomFloat(9.0f, 15.0f),
            randomFloat(12.0f, 20.0f),
            randomFloat(8.0f, 14.0f)
        );

        die2AngularVelocity = glm::vec3(
            randomFloat(-15.0f, -9.0f),
            randomFloat(11.0f, 19.0f),
            randomFloat(-14.0f, -8.0f)
        );

        // Initial rotation starts random so throws do not look identical.
        die1Rotation = glm::vec3(
            randomFloat(0.0f, glm::radians(360.0f)),
            randomFloat(0.0f, glm::radians(360.0f)),
            randomFloat(0.0f, glm::radians(360.0f))
        );

        die2Rotation = glm::vec3(
            randomFloat(0.0f, glm::radians(360.0f)),
            randomFloat(0.0f, glm::radians(360.0f)),
            randomFloat(0.0f, glm::radians(360.0f))
        );

        die1Value = diceDistribution(randomEngine);
        die2Value = diceDistribution(randomEngine);

        std::cout << "Throwing dice with physics...\n";
    }



    void updateDice(float deltaT) {
        if (!diceRolling) {
            return;
        }

        diceRollTimer += deltaT;

        updateSingleDiePhysics(
            die1Position,
            die1Velocity,
            die1Rotation,
            die1AngularVelocity,
            die1Sleeping,
            die1Value,
            deltaT,
            false
        );

        updateSingleDiePhysics(
            die2Position,
            die2Velocity,
            die2Rotation,
            die2AngularVelocity,
            die2Sleeping,
            die2Value,
            deltaT,
            true
        );
        resolveDiceDiceCollision(deltaT);
        // If both dice have stopped, the roll is finished.
        if (die1Sleeping && die2Sleeping) {
            finishDiceRoll();
            return;
        }

        // Safety stop in case numerical damping takes too long.
        if (diceRollTimer >= diceRollMaxDuration) {
            forceStopDie(
                die1Position,
                die1Velocity,
                die1Rotation,
                die1AngularVelocity,
                die1Sleeping,
                die1Value,
                false
            );

            forceStopDie(
                die2Position,
                die2Velocity,
                die2Rotation,
                die2AngularVelocity,
                die2Sleeping,
                die2Value,
                true
            );

            finishDiceRoll();
        }
    }

void updateSingleDiePhysics(
    glm::vec3& position,
    glm::vec3& velocity,
    glm::vec3& rotation,
    glm::vec3& angularVelocity,
    bool& sleeping,
    int& value,
    float deltaT,
    bool secondDie
) {
    if (sleeping) {
        return;
    }

    // Gravity affects vertical velocity.
    velocity.y += DICE_GRAVITY * deltaT;

    // Position changes based on velocity.
    position += velocity * deltaT;

    // Rotation changes based on angular velocity.
    rotation += angularVelocity * deltaT;

    // Air drag slowly reduces horizontal movement.
    float frameDrag = std::pow(DICE_AIR_DRAG, deltaT * 60.0f);
    velocity.x *= frameDrag;
    velocity.z *= frameDrag;

    // Keep dice inside the board area.
    // When they hit the border, they bounce back slightly.
    if (position.x < DICE_MIN_X) {
        position.x = DICE_MIN_X;
        velocity.x = std::abs(velocity.x) * DICE_BOUNCE_FACTOR;
    }

    if (position.x > DICE_MAX_X) {
        position.x = DICE_MAX_X;
        velocity.x = -std::abs(velocity.x) * DICE_BOUNCE_FACTOR;
    }

    if (position.z < DICE_MIN_Z) {
        position.z = DICE_MIN_Z;
        velocity.z = std::abs(velocity.z) * DICE_BOUNCE_FACTOR;
    }

    if (position.z > DICE_MAX_Z) {
        position.z = DICE_MAX_Z;
        velocity.z = -std::abs(velocity.z) * DICE_BOUNCE_FACTOR;
    }

    // Collision with the board plane.
   // Collision with the board plane.
// We clamp the CENTER of the die to DICE_REST_Y.
// Since DICE_REST_Y is above the board surface, the visible die stays above the board.
        // Collision with the board plane.
        // We clamp the CENTER of the die to DICE_REST_Y.
        // Since DICE_REST_Y is above the board surface, the visible die stays above the board.
        if (position.y <= DICE_REST_Y) {
            position.y = DICE_REST_Y;

            if (velocity.y < 0.0f) {
                // Bounce upward, but lose energy.
                velocity.y = -velocity.y * DICE_BOUNCE_FACTOR;

                // Contact friction reduces sliding.
                velocity.x *= DICE_FRICTION;
                velocity.z *= DICE_FRICTION;

                // Rotational damping reduces spin after each bounce.
                angularVelocity *= DICE_ANGULAR_DAMPING;

                // Extra damping for small bounces so the die settles instead of jittering.
                if (velocity.y < 0.32f) {
                    velocity.y = 0.0f;
                    angularVelocity *= 0.60f;
                }

                // Change displayed value on each meaningful bounce.
                if (velocity.y > 0.35f) {
                    value = diceDistribution(randomEngine);
                }
            }
        }

    float horizontalSpeed = glm::length(glm::vec2(velocity.x, velocity.z));
    float angularSpeed = glm::length(angularVelocity);

    bool restingOnBoard = position.y <= DICE_REST_Y + 0.001f;
    bool movingSlowly = horizontalSpeed < 0.06f && std::abs(velocity.y) < 0.02f;
    bool spinningSlowly = angularSpeed < 0.20f;

    if (restingOnBoard && movingSlowly && spinningSlowly) {
        forceStopDie(
            position,
            velocity,
            rotation,
            angularVelocity,
            sleeping,
            value,
            secondDie
        );
    }
}

    void resolveDiceDiceCollision(float deltaT) {
    glm::vec3 difference = die2Position - die1Position;

    // We mostly care about collision in the XZ plane because the dice are on the board.
    glm::vec2 differenceXZ = glm::vec2(difference.x, difference.z);

    float distance = glm::length(differenceXZ);
    float minimumDistance = DICE_COLLISION_RADIUS * 2.0f;

    // If the dice are exactly on top of each other, choose a safe direction.
    if (distance < 0.0001f) {
        differenceXZ = glm::vec2(1.0f, 0.0f);
        distance = 1.0f;
    }

    if (distance >= minimumDistance) {
        return;
    }

    glm::vec2 normalXZ = differenceXZ / distance;

    float penetration = minimumDistance - distance;

    // Push each die half the penetration distance away from the other.
    // This removes visual overlap immediately.
    glm::vec2 correction = normalXZ * (penetration * 0.5f);

    die1Position.x -= correction.x;
    die1Position.z -= correction.y;

    die2Position.x += correction.x;
    die2Position.z += correction.y;

    // Keep both dice inside the board after correction.
    die1Position.x = std::clamp(die1Position.x, DICE_MIN_X, DICE_MAX_X);
    die1Position.z = std::clamp(die1Position.z, DICE_MIN_Z, DICE_MAX_Z);

    die2Position.x = std::clamp(die2Position.x, DICE_MIN_X, DICE_MAX_X);
    die2Position.z = std::clamp(die2Position.z, DICE_MIN_Z, DICE_MAX_Z);

    // Relative velocity tells us whether the dice are moving toward each other.
    glm::vec2 v1 = glm::vec2(die1Velocity.x, die1Velocity.z);
    glm::vec2 v2 = glm::vec2(die2Velocity.x, die2Velocity.z);

    glm::vec2 relativeVelocity = v2 - v1;
    float velocityAlongNormal = glm::dot(relativeVelocity, normalXZ);

    // If they are already moving apart, no impulse is needed.
    if (velocityAlongNormal > 0.0f) {
        return;
    }

    // Equal mass collision impulse.
    // Restitution controls bounciness.
    float impulseMagnitude =
        -(1.0f + DICE_DICE_RESTITUTION) * velocityAlongNormal / 2.0f;

    glm::vec2 impulse = impulseMagnitude * normalXZ;

    v1 -= impulse;
    v2 += impulse;

    die1Velocity.x = v1.x;
    die1Velocity.z = v1.y;

    die2Velocity.x = v2.x;
    die2Velocity.z = v2.y;

    // Add some extra spin when the dice collide.
    die1AngularVelocity += glm::vec3(
        randomFloat(-1.5f, 1.5f),
        randomFloat(1.0f, 2.2f),
        randomFloat(-1.5f, 1.5f)
    );

    die2AngularVelocity += glm::vec3(
        randomFloat(-1.5f, 1.5f),
        randomFloat(1.0f, 2.2f),
        randomFloat(-1.5f, 1.5f)
    );

    // If a sleeping die is hit by the other die, wake it up again.
    die1Sleeping = false;
    die2Sleeping = false;
}

    void forceStopDie(
        glm::vec3& position,
        glm::vec3& velocity,
        glm::vec3& rotation,
        glm::vec3& angularVelocity,
        bool& sleeping,
        int& value,
        bool secondDie
    ) {
        sleeping = true;

        // Clamp to valid resting height.
        // This prevents the visual mesh from ending inside the board.
        position.y = DICE_REST_Y;

        // Keep final position on the board.
        position.x = std::clamp(position.x, DICE_MIN_X, DICE_MAX_X);
        position.z = std::clamp(position.z, DICE_MIN_Z, DICE_MAX_Z);

        velocity = glm::vec3(0.0f);
        angularVelocity = glm::vec3(0.0f);

        value = diceDistribution(randomEngine);

        // Important: final rotation should be face-aligned, not arbitrary.
        // Otherwise the die can visually stop on a corner and penetrate the board.
        rotation = finalDiceRotation(value, secondDie);
    }


    glm::vec3 finalDiceRotation(int value, bool secondDie) const {
        // The die must stop on clean 90-degree rotations.
        // That makes the cube visually aligned with the board plane.
        //
        // We vary only by 90-degree steps so it looks different per result,
        // but never ends tilted into or above the ground.
        float yTurn = secondDie ? glm::radians(90.0f) : glm::radians(0.0f);

        switch (value) {
        case 1:
            return glm::vec3(
                glm::radians(0.0f),
                yTurn,
                glm::radians(0.0f)
            );

        case 2:
            return glm::vec3(
                glm::radians(90.0f),
                yTurn,
                glm::radians(0.0f)
            );

        case 3:
            return glm::vec3(
                glm::radians(0.0f),
                yTurn,
                glm::radians(90.0f)
            );

        case 4:
            return glm::vec3(
                glm::radians(0.0f),
                yTurn + glm::radians(90.0f),
                glm::radians(90.0f)
            );

        case 5:
            return glm::vec3(
                glm::radians(270.0f),
                yTurn,
                glm::radians(0.0f)
            );

        case 6:
        default:
            return glm::vec3(
                glm::radians(180.0f),
                yTurn,
                glm::radians(0.0f)
            );
        }
    }


void finishDiceRoll() {
    diceRolling = false;

    movementPoints = die1Value + die2Value;

    std::cout
        << "Dice result: "
        << die1Value << " + " << die2Value
        << " = " << movementPoints
        << " movement points\n";
}
    // -------------------------------
    // Main game/camera logic
    // -------------------------------

    void tryToggleCornerLight(int key, int lightIndex) {
        if (lightIndex < 0 || lightIndex >= NUM_CORNER_LIGHTS) {
            return;
        }

        if (
            lightToggleCooldown[lightIndex] <= 0.0f &&
            glfwGetKey(window, key) == GLFW_PRESS
        ) {
            cornerLightEnabled[lightIndex] = !cornerLightEnabled[lightIndex];

            std::cout
                << "Light " << (lightIndex + 1)
                << (cornerLightEnabled[lightIndex] ? " ON\n" : " OFF\n");

            lightToggleCooldown[lightIndex] = 0.30f;
        }
    }


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
        // Token movement + light toggles
        // -------------------------------

        moveCooldown -= deltaT;

        for (int i = 0; i < NUM_CORNER_LIGHTS; i++) {
            lightToggleCooldown[i] -= deltaT;
        }

        // 1/2/3/4 toggle the four corner lights.
        tryToggleCornerLight(GLFW_KEY_1, 0);
        tryToggleCornerLight(GLFW_KEY_2, 1);
        tryToggleCornerLight(GLFW_KEY_3, 2);
        tryToggleCornerLight(GLFW_KEY_4, 3);

        // I/J/K/L always move the token again.
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

        glm::mat4 Prj = glm::perspectiveRH_ZO(
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
        const glm::vec3& rotation,
        int value,
        bool secondDie
    ) const {
        glm::mat4 M = glm::mat4(1.0f);

        // Translation: where the die is in the world.
        M = glm::translate(M, position);

        // Rotation from the physics simulation.
        // When the die is sleeping, forceStopDie() has already snapped this rotation
        // to clean 90-degree steps, so the cube rests flat on the board.
        M = glm::rotate(M, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        M = glm::rotate(M, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        M = glm::rotate(M, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));

        // Do NOT add extra angle here.
        // Even a small 7-degree rotation makes the final die look misaligned
        // if the die mesh has visible edges/details.

        // Dice size.
        M = glm::scale(M, glm::vec3(0.45f, 0.45f, 0.45f));

        return M;
    }


    glm::vec3 lampPosition() const {
        // The shadow map still uses light 1.
        // The scene has four lights for illumination, but only one shadow-casting light.
        return glm::vec3(cornerLightPositions[0]);
    }

    glm::vec3 lampTarget() const {
        // Aim at the board center, slightly above the board surface.
        return glm::vec3(0.0f, 0.35f, 0.0f);
    }

    glm::mat4 computeLightViewProj() const {
        glm::mat4 lightView = glm::lookAt(
            lampPosition(),
            lampTarget(),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        glm::mat4 lightProj = glm::perspectiveRH_ZO(
            glm::radians(78.0f),
            1.0f,
            0.25f,
            16.0f
        );

        // Vulkan clip-space correction.
        lightProj[1][1] *= -1.0f;

        return lightProj * lightView;
    }
    glm::vec4 lampBulbMaterial(int lightIndex) const {
        if (cornerLightEnabled[lightIndex]) {
            // Alpha > 1.5 means emissive in Arena.frag.
            return glm::vec4(1.0f, 0.82f, 0.35f, 2.0f);
        }

        // Dark non-emissive bulb when off.
        return glm::vec4(0.05f, 0.04f, 0.03f, 0.75f);
    }
    glm::vec4 objectMaterialColor(int instanceId) const {
        switch (instanceId) {
        case 0:
            // table_surface
            return glm::vec4(0.45f, 0.25f, 0.10f, 0.25f);

        case 2:
            // player_token
            return glm::vec4(1.0f, 0.25f, 0.10f, 0.25f);

        case 3:
        case 4:
            // blocked cells / obstacles
            return glm::vec4(0.25f, 0.25f, 0.28f, 0.25f);

        case 5:
        case 6:
            // dice
            return glm::vec4(0.95f, 0.95f, 0.90f, 0.25f);

        case LAMP_1_POST_INSTANCE_INDEX:
        case LAMP_2_POST_INSTANCE_INDEX:
        case LAMP_3_POST_INSTANCE_INDEX:
        case LAMP_4_POST_INSTANCE_INDEX:
            // lamp posts
            return glm::vec4(0.18f, 0.15f, 0.10f, 0.65f);

        case LAMP_1_BULB_INSTANCE_INDEX:
            return lampBulbMaterial(0);

        case LAMP_2_BULB_INSTANCE_INDEX:
            return lampBulbMaterial(1);

        case LAMP_3_BULB_INSTANCE_INDEX:
            return lampBulbMaterial(2);

        case LAMP_4_BULB_INSTANCE_INDEX:
            return lampBulbMaterial(3);
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
            // white pieces
            return glm::vec4(0.95f, 0.95f, 0.90f, 1.0f);

        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
            // black pieces
            return glm::vec4(0.05f, 0.05f, 0.05f, 1.0f);
        case DICE_TRAY_FLOOR_INSTANCE_INDEX:
            // dice tray floor
            return glm::vec4(0.32f, 0.18f, 0.08f, 0.55f);

        case DICE_TRAY_WALL_LEFT_INSTANCE_INDEX:
        case DICE_TRAY_WALL_RIGHT_INSTANCE_INDEX:
        case DICE_TRAY_WALL_BACK_INSTANCE_INDEX:
        case DICE_TRAY_WALL_FRONT_INSTANCE_INDEX:
            // dice tray walls
            return glm::vec4(0.22f, 0.12f, 0.05f, 0.75f);
        default:
            return glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        }
    }


    void updateDiceInstances(float deltaT) {
        if (SC.TI == nullptr) {
            return;
        }

        if (SC.TI[0].InstanceCount <= DIE_2_INSTANCE_INDEX) {
            return;
        }

        SC.TI[0].I[DIE_1_INSTANCE_INDEX].Wm =
            diceModelMatrix(die1Position, die1Rotation, die1Value, false);

        SC.TI[0].I[DIE_2_INSTANCE_INDEX].Wm =
            diceModelMatrix(die2Position, die2Rotation, die2Value, true);
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
    glm::mat4 lampBulbModelMatrix(int lightIndex) const {
        glm::vec3 lampPos = glm::vec3(cornerLightPositions[lightIndex]);

        glm::mat4 M = glm::mat4(1.0f);

        M = glm::translate(M, lampPos);
        M = glm::scale(M, glm::vec3(0.34f, 0.34f, 0.34f));

        return M;
    }


    glm::mat4 lampPostModelMatrix(int lightIndex) const {
        glm::vec3 lampPos = glm::vec3(cornerLightPositions[lightIndex]);

        float baseY = 0.05f;
        float postHeight = std::max(0.25f, lampPos.y - baseY);
        float centerY = baseY + postHeight * 0.5f;

        glm::mat4 M = glm::mat4(1.0f);

        M = glm::translate(
            M,
            glm::vec3(lampPos.x, centerY, lampPos.z)
        );

        M = glm::scale(
            M,
            glm::vec3(0.10f, postHeight, 0.10f)
        );

        return M;
    }

    void updateLampInstances() {
        if (SC.TI == nullptr) {
            return;
        }

        if (SC.TI[0].InstanceCount <= LAMP_4_BULB_INSTANCE_INDEX) {
            return;
        }

        SC.TI[0].I[LAMP_1_POST_INSTANCE_INDEX].Wm = lampPostModelMatrix(0);
        SC.TI[0].I[LAMP_1_BULB_INSTANCE_INDEX].Wm = lampBulbModelMatrix(0);

        SC.TI[0].I[LAMP_2_POST_INSTANCE_INDEX].Wm = lampPostModelMatrix(1);
        SC.TI[0].I[LAMP_2_BULB_INSTANCE_INDEX].Wm = lampBulbModelMatrix(1);

        SC.TI[0].I[LAMP_3_POST_INSTANCE_INDEX].Wm = lampPostModelMatrix(2);
        SC.TI[0].I[LAMP_3_BULB_INSTANCE_INDEX].Wm = lampBulbModelMatrix(2);

        SC.TI[0].I[LAMP_4_POST_INSTANCE_INDEX].Wm = lampPostModelMatrix(3);
        SC.TI[0].I[LAMP_4_BULB_INSTANCE_INDEX].Wm = lampBulbModelMatrix(3);
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