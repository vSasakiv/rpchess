#include <sstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <cmath>
#include <array>
#include <vector>
#include <fstream>
#include <string>

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
    alignas(16) glm::mat4 lightViewProj[4];

    alignas(16) glm::vec4 lightPositions[4];
    alignas(16) glm::vec4 lightColors[4];
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
    std::array<RenderPass, 4> RPshadow;

    Pipeline P;
    std::array<Pipeline, 4> Pshadow;

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

    static constexpr int LAMP_1_POST_INSTANCE_INDEX = 19;
    static constexpr int LAMP_1_BULB_INSTANCE_INDEX = 20;

    static constexpr int LAMP_2_POST_INSTANCE_INDEX = 21;
    static constexpr int LAMP_2_BULB_INSTANCE_INDEX = 22;

    static constexpr int LAMP_3_POST_INSTANCE_INDEX = 23;
    static constexpr int LAMP_3_BULB_INSTANCE_INDEX = 24;

    static constexpr int LAMP_4_POST_INSTANCE_INDEX = 25;
    static constexpr int LAMP_4_BULB_INSTANCE_INDEX = 26;
    static constexpr int DICE_TRAY_INDEX = 27;
    static constexpr int DIE_1_PIP_START_INDEX = 28;
    static constexpr int DIE_2_PIP_START_INDEX = 34;
    static constexpr int PIPS_PER_DIE = 6;
    static constexpr int LAST_DICE_PIP_INDEX = DIE_2_PIP_START_INDEX + PIPS_PER_DIE - 1;


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
        glm::vec4(-4.2f, 2.4f, -4.2f, 1.0f),
        glm::vec4( 4.2f, 2.5f, -4.2f, 1.0f),
        glm::vec4(-4.2f, 2.4f,  4.2f, 1.0f),
        glm::vec4( 4.2f, 2.4f,  4.2f, 1.0f)
    };

    // rgb = color, w = intensity.
    glm::vec4 cornerLightColors[NUM_CORNER_LIGHTS] = {
        glm::vec4(1.0f, 0.90f, 0.72f, 1.35f),
        glm::vec4(1.0f, 0.90f, 0.72f, 0.55f),
        glm::vec4(1.0f, 0.90f, 0.72f, 0.55f),
        glm::vec4(1.0f, 0.90f, 0.72f, 0.55f)
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

    // -------------------------------
    // Game loop state
    // -------------------------------

    enum class GamePhase {
        WaitingForRoll,
        Moving,
        Attacking,
        GameOver
    };

    enum class BoardItemType {
        Empty,
        Obstacle,
        Rook,
        Bishop,
        Knight,
        Queen,
        King
    };

    struct BoardItem {
        bool active = false;
        BoardItemType type = BoardItemType::Empty;
        int row = 0;
        int col = 0;
        int instanceId = -1;
    };

    static constexpr int DYNAMIC_ITEM_COUNT = 12;


    std::array<int, DYNAMIC_ITEM_COUNT> dynamicItemInstanceIds = {
        7, 8, 9, 10, 11, 12,
        13, 14, 15, 16, 17, 18
    };

    std::array<BoardItem, DYNAMIC_ITEM_COUNT> dynamicItems{};

    GamePhase gamePhase = GamePhase::WaitingForRoll;

    int roundNumber = 1;
    int roundsSurvived = 0;
    int highScore = 0;

    std::string statusMessage = "Roll dice to start.";


    // -------------------------------
    // Attack animation state
    // -------------------------------

    bool attackAnimationActive = false;
    float attackAnimationTimer = 0.0f;

    static constexpr float ATTACK_ANIMATION_DURATION = 1.0f;

    BoardItem currentAttacker{};
    bool hasCurrentAttacker = false;

    // Board/token height is around y = 0.38 in this scene.
    // The dice collision uses the center of the die, so the center must be ABOVE the board.
    static constexpr float DICE_BOARD_Y = 0.38f;
    static constexpr float DICE_REST_Y = 0.48f;

    static constexpr float DICE_GRAVITY = -8.8f;

    // Higher bounce factor = the dice keep more energy after impact.
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

    // Dice tray area.
    // The dice now roll inside the side tray, not on the board.
    static constexpr float DICE_TRAY_CENTER_X = 5.8f;
    static constexpr float DICE_TRAY_CENTER_Z = 0.0f;

    static constexpr float DICE_TRAY_HALF_X = 1.10f;
    static constexpr float DICE_TRAY_HALF_Z = 0.90f;

    // Keep dice away from the tray walls visually.
    static constexpr float DICE_TRAY_DICE_MARGIN = 0.38f;

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

    // Sets the initial window configuration used before Vulkan creates the window.
    void setWindowParameters() {
        windowWidth = 1280;
        windowHeight = 720;
        windowTitle = "CarlsenQuest";
        windowResizable = true;

        Ar = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    }

    // Called when the window is resized.
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

    // Defines descriptor layouts, vertex format, render passes, pipelines, scene technique,
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

        // NOTE: the 4th field of each binding (linkSize) is used by DescriptorSet::init
        // as an offset into a SHARED imageInfo array across all image-type bindings in
        // this layout. It must be a running offset (0, 1, 2, 3, ...), not 0 for every
        // binding
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
      },
      {
          2,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          VK_SHADER_STAGE_FRAGMENT_BIT,
          1,
          1
      },
      {
          3,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          VK_SHADER_STAGE_FRAGMENT_BIT,
          2,
          1
      },
      {
          4,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          VK_SHADER_STAGE_FRAGMENT_BIT,
          3,
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
        for (int i = 0; i < 4; i++) {
            RPshadow[i].init(
                this,
                2048,
                2048,
                -1,
                RenderPass::getStandardAttchmentsProperties(AT_DEPTH_ONLY, this),
                RenderPass::getStandardDependencies(ATDEP_DEPTH_TRANS),
                true
            );
        }

        // Shadow pipeline: writes only depth from the lamp view.
        // Shadow pipelines: one per light, each writes only depth from that lamp's view.
        // A separate Pipeline object per light is required because Pipeline::create()
        // binds to a specific RenderPass at creation time; reusing one Pipeline object
        // across multiple RenderPass objects left passes 1-3 using pass 0's viewport/
        // framebuffer state,
        for (int i = 0; i < 4; i++) {
            Pshadow[i].init(
                this,
                &VD,
                "shaders/Shadow.vert.spv",
                "shaders/Shadow.frag.spv",
                {&DSLshadowGlobal, &DSLshadowLocal}
            );

            Pshadow[i].setCullMode(VK_CULL_MODE_NONE);
            Pshadow[i].setCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);
        }

        // Main arena pipeline.
        P.init(
            this,
            &VD,
            "shaders/Arena.vert.spv",
            "shaders/Arena.frag.spv",
            {&DSLglobal, &DSLlocal}
        );

        // Disabled because our custom cube may have mixed winding.
        P.setCullMode(VK_CULL_MODE_NONE);
        P.setCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

        // Descriptor pool size.
        DPSZs.uniformBlocksInPool = 120;
        DPSZs.texturesInPool = 120;
        DPSZs.setsInPool = 120;

        // Scene support names.
        // These must match scene.json.
        VDRs.resize(1);
        VDRs[0].init("VDposNormUV", &VD);

        TextureDefs shadowMapTexture0{};
        shadowMapTexture0.fromInstance = false;
        shadowMapTexture0.pos = 0;
        shadowMapTexture0.info = {};

        TextureDefs shadowMapTexture1{};
        shadowMapTexture1.fromInstance = false;
        shadowMapTexture1.pos = 1;
        shadowMapTexture1.info = {};

        TextureDefs shadowMapTexture2{};
        shadowMapTexture2.fromInstance = false;
        shadowMapTexture2.pos = 2;
        shadowMapTexture2.info = {};

        TextureDefs shadowMapTexture3{};
        shadowMapTexture3.fromInstance = false;
        shadowMapTexture3.pos = 3;
        shadowMapTexture3.info = {};

        TextureDefs objectTexture{};
        objectTexture.fromInstance = true;
        objectTexture.pos = 0;
        objectTexture.info = {};

        PRs.resize(1);
        PRs.resize(1);
        PRs[0].init(
            "ArenaTechnique",
            {
                // Pass 0: shadow from light 1.
                {
                    &Pshadow[0],
                    {
                        {},
                        {}
                    }
                },

                // Pass 1: shadow from light 2.
                {
                    &Pshadow[1],
                    {
                        {},
                        {}
                    }
                },

                // Pass 2: shadow from light 3.
                {
                    &Pshadow[2],
                    {
                        {},
                        {}
                    }
                },

                // Pass 3: shadow from light 4.
                {
                    &Pshadow[3],
                    {
                        {},
                        {}
                    }
                },

                // Pass 4: main scene render.
                {
                    &P,
                    {
                        {
                            shadowMapTexture0,
                            shadowMapTexture1,
                            shadowMapTexture2,
                            shadowMapTexture3
                        },
                        {
                            objectTexture
                        }
                    }
                }
            },
            1,
            &VD
        );


        if (SC.init(this, 5, VDRs, PRs, "assets/scenes/scene.json") != 0) {
            std::cout << "ERROR LOADING THE SCENE\n";
            exit(0);
        }
        loadHighScore();
        resetGame();

        std::cout << "Scene instance count: " << SC.TI[0].InstanceCount << "\n";
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
    // This is where render passes and pipelines become real Vulkan objects,
    // also where the four shadow maps are  connected to the main
    void pipelinesAndDescriptorSetsInit() {
        for (int i = 0; i < 4; i++) {
            RPshadow[i].create();
        }

        RP.create();

        for (int i = 0; i < 4; i++) {
            Pshadow[i].create(&RPshadow[i]);
        }

        P.create(&RP);

        PRs[0].PT[4].texDefs[0][0].info =
            RPshadow[0].attachments[0].getViewAndSampler();

        PRs[0].PT[4].texDefs[0][1].info =
            RPshadow[1].attachments[0].getViewAndSampler();

        PRs[0].PT[4].texDefs[0][2].info =
            RPshadow[2].attachments[0].getViewAndSampler();

        PRs[0].PT[4].texDefs[0][3].info =
            RPshadow[3].attachments[0].getViewAndSampler();

        SC.pipelinesAndDescriptorSetsInit();
        txt.pipelinesAndDescriptorSetsInit();
    }

    // Called when pipelines/render passes/descriptors must be recreated, for example during resize.
    void pipelinesAndDescriptorSetsCleanup() {
        for (int i = 0; i < 4; i++) {
            Pshadow[i].cleanup();
        }
        P.cleanup();

        for (int i = 0; i < 4; i++) {
            RPshadow[i].cleanup();
        }

        RP.cleanup();

        SC.pipelinesAndDescriptorSetsCleanup();
        txt.pipelinesAndDescriptorSetsCleanup();
    }
    // Final shutdown cleanup.
    void localCleanup() {
        DSLlocal.cleanup();
        DSLglobal.cleanup();
        DSLshadowGlobal.cleanup();
        DSLshadowLocal.cleanup();

        for (int i = 0; i < 4; i++) {
            Pshadow[i].destroy();
        }
        P.destroy();

        for (int i = 0; i < 4; i++) {
            RPshadow[i].destroy();
        }

        RP.destroy();

        SC.localCleanup();
        txt.localCleanup();
    }


    // -------------------------------
    // Command buffer
    // -------------------------------

    // Converts the generic void* pointer back to our application object and calls populateCommandBuffer().
    static void populateCommandBufferAccess(
        VkCommandBuffer commandBuffer,
        int currentImage,
        void* params
    ) {
        auto* app = static_cast<TabletopDiceRPGArena*>(params);
        app->populateCommandBuffer(commandBuffer, currentImage);
    }

    // Records the full frame rendering order into the command buffer.
    // First renders four shadow maps, one per light, then renders the final visible scene in pass 4.
    void populateCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
        for (int lightIndex = 0; lightIndex < 4; lightIndex++) {
            RPshadow[lightIndex].begin(commandBuffer, currentImage);
            populateShadowCommandBuffer(commandBuffer, currentImage, lightIndex);
            RPshadow[lightIndex].end(commandBuffer);
        }

        RP.begin(commandBuffer, currentImage);

        // Main render pass is now pass 4.
        SC.populateCommandBuffer(commandBuffer, 4, currentImage);

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
        // Large receiver surfaces should receive shadows, not cast them.
        if (instanceId == 0 ||      // table_surface
            instanceId == 1 ||      // game_board
            instanceId == DICE_TRAY_INDEX ||
            isLampInstance(instanceId) ||
            isDicePipInstance(instanceId)) {
            return false;
            }

        // All possible dynamic enemy pieces must always be present
        // in the shadow command buffer.
        if (canSpawnAsEnemyInstance(instanceId)) {
            return true;
        }

        // Player token.
        if (instanceId == TOKEN_INSTANCE_INDEX) {
            return true;
        }

        // Dice.
        if (instanceId == DIE_1_INSTANCE_INDEX ||
            instanceId == DIE_2_INSTANCE_INDEX) {
            return true;
            }

        // Fixed obstacles.
        if (instanceId == 3 || instanceId == 4) {
            return true;
        }

        return false;
    }

    // Draws all shadow-casting objects for one shadow pass.
    void populateShadowCommandBuffer(
        VkCommandBuffer commandBuffer,
        int currentImage,
        int passId
    ) {
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
                    instance.DS[passId][setId]->bind(
                        commandBuffer,
                        *pipeline,
                        setId,
                        currentImage
                    );
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


    // Updates game logic, model matrices, light matrices, global lighting data,
    // shadow-pass uniforms, main-pass uniforms, and HUD text before the frame is rendered.
    void updateUniformBuffer(uint32_t currentImage) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    float deltaT = GameLogic();

    updateTokenInstance();
    updateDiceInstances(deltaT);
    updateDicePipInstances();
    updateDynamicBoardItemInstances();
    // updateLampInstances();

    std::array<glm::mat4, 4> lightViewProj{};

    for (int i = 0; i < 4; i++) {
        lightViewProj[i] = computeLightViewProj(i);
    }

    std::array<ShadowGlobalUniformBufferObject, 4> sgubo{};

    for (int i = 0; i < 4; i++) {
        sgubo[i].lightViewProj = lightViewProj[i];
    }

    GlobalUniformBufferObject gubo{};

    for (int i = 0; i < 4; i++) {
        gubo.lightViewProj[i] = lightViewProj[i];
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
    // z = normal offset
    // w = unused
    gubo.shadowParams = glm::vec4(0.0005f, 0.95f, 0.001f, 1.0f);

    UniformBufferObject ubo{};
    ShadowLocalUniformBufferObject slubo{};

    for (int instanceId = 0; instanceId < SC.TI[0].InstanceCount; instanceId++) {
        glm::mat4 model = SC.TI[0].I[instanceId].Wm;

        slubo.mMat = model;

        // Shadow passes 0, 1, 2, 3.
        for (int shadowPass = 0; shadowPass < 4; shadowPass++) {
            SC.TI[0].I[instanceId].DS[shadowPass][0]->map(
                currentImage,
                &sgubo[shadowPass],
                0
            );

            SC.TI[0].I[instanceId].DS[shadowPass][1]->map(
                currentImage,
                &slubo,
                0
            );
        }

        // Main pass 4.
        ubo.mMat = model;
        ubo.mvpMat = ViewPrj * model;
        ubo.materialColor = objectMaterialColor(instanceId);

        SC.TI[0].I[instanceId].DS[4][0]->map(currentImage, &gubo, 0);
        SC.TI[0].I[instanceId].DS[4][1]->map(currentImage, &ubo, 0);
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

            oss << "Round: " << roundNumber << "\n";
            oss << "Survived: " << roundsSurvived << "\n";
            oss << "High score: " << highScore << "\n";
            oss << "Dice: " << die1Value << " + " << die2Value << "\n";
            oss << "Move points: " << movementPoints << "\n";

            if (gamePhase == GamePhase::WaitingForRoll) {
                oss << "State: ROLL\n";
                oss << "SPACE: roll dice\n";
            } else if (gamePhase == GamePhase::Moving) {
                oss << "State: MOVE\n";
                oss << "I/J/K/L: move player\n";
            } else if (gamePhase == GamePhase::Attacking) {
                oss << "State: ATTACK!\n";
                oss << "Incoming attack...\n";
            } else {
                oss << "State: GAME OVER\n";
                oss << "R: restart\n";
            }

            oss << "Lights: ";
            for (int i = 0; i < NUM_CORNER_LIGHTS; i++) {
                oss << (cornerLightEnabled[i] ? "ON" : "OFF");

                if (i < NUM_CORNER_LIGHTS - 1) {
                    oss << " | ";
                }
            }
            oss << "\n";

            oss << statusMessage << "\n";

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
        if (gamePhase != GamePhase::WaitingForRoll) {
            return;
        }

        if (diceRolling) {
            return;
        }

        diceRolling = true;
        diceRollTimer = 0.0f;

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


    // Advances the dice simulation while the dice are rolling.
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
    // Applies gravity, velocity integration, angular rotation, air drag, tray-wall bounces,
    // floor collision, friction, damping, value changes on bounce, and sleep detection.
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

    // We  care about collision in the XZ plane because the dice are on the board.
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
    // This removes visual overlap.
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
    // Forces one die into a stable final state.
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
        // This mapping matches the actual textured cube from your OBJ + UV layout:
        //
        // top    = 2
        // bottom = 5
        // front  = 4
        // back   = 3
        // right  = 1
        // left   = 6
        //
        // We therefore rotate the cube so the requested number becomes the top face.

        switch (value) {
        case 1:
            // right -> top
            return glm::vec3(
                0.0f,
                0.0f,
                glm::radians(90.0f)
            );

        case 2:
            // already on top
            return glm::vec3(
                0.0f,
                0.0f,
                0.0f
            );

        case 3:
            // back -> top
            return glm::vec3(
                glm::radians(90.0f),
                0.0f,
                0.0f
            );

        case 4:
            // front -> top
            return glm::vec3(
                glm::radians(-90.0f),
                0.0f,
                0.0f
            );

        case 5:
            // bottom -> top
            return glm::vec3(
                glm::radians(180.0f),
                0.0f,
                0.0f
            );

        case 6:
        default:
            // left -> top
            return glm::vec3(
                0.0f,
                0.0f,
                glm::radians(-90.0f)
            );
        }
    }

    void finishDiceRoll() {
        diceRolling = false;

        movementPoints = die1Value + die2Value;
        gamePhase = GamePhase::Moving;

        std::ostringstream oss;
        oss << "Rolled "
            << die1Value
            << " + "
            << die2Value
            << " = "
            << movementPoints
            << ". Move the player.";

        statusMessage = oss.str();

        std::cout << statusMessage << "\n";
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
    void loadHighScore() {
        std::ifstream file("highscore.txt");

        if (file.is_open()) {
            file >> highScore;
        }
    }

    void saveHighScore() {
        std::ofstream file("highscore.txt");

        if (file.is_open()) {
            file << highScore;
        }
    }

    const char* boardItemTypeName(BoardItemType type) const {
        switch (type) {
        case BoardItemType::Obstacle:
            return "Obstacle";
        case BoardItemType::Rook:
            return "Rook";
        case BoardItemType::Bishop:
            return "Bishop";
        case BoardItemType::Knight:
            return "Knight";
        case BoardItemType::Queen:
            return "Queen";
        case BoardItemType::King:
            return "King";
        default:
            return "Empty";
        }
    }


    bool isDynamicItemInstance(int instanceId) const {
        for (const BoardItem& item : dynamicItems) {
            if (item.instanceId == instanceId) {
                return true;
            }
        }

        return false;
    }
    bool isInactiveDynamicItemInstance(int instanceId) const {
        for (const BoardItem& item : dynamicItems) {
            if (item.instanceId == instanceId && !item.active) {
                return true;
            }
        }

        return false;
    }


    bool isActiveDynamicItemInstance(int instanceId) const {
        for (const BoardItem& item : dynamicItems) {
            if (item.instanceId == instanceId && item.active) {
                return true;
            }
        }

        return false;
    }





    bool canSpawnAsEnemyInstance(int instanceId) const {
        switch (instanceId) {
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
            return true;

        default:
            return false;
        }
    }


    BoardItemType dynamicItemTypeFromInstance(int instanceId) const {
        for (const BoardItem& item : dynamicItems) {
            if (item.instanceId == instanceId) {
                return item.type;
            }
        }

        return BoardItemType::Empty;
    }


    BoardItemType boardItemTypeForInstance(int instanceId) const {
        switch (instanceId) {
        case 8:
        case 14:
            return BoardItemType::Knight;

        case 9:
        case 15:
            return BoardItemType::Bishop;

        case 10:
        case 16:
            return BoardItemType::Rook;

        case 11:
        case 17:
            return BoardItemType::Queen;

        case 12:
        case 18:
            return BoardItemType::King;

        default:
            return BoardItemType::Empty;
        }
    }

    bool isCurrentAttackerInstance(int instanceId) const {
        return hasCurrentAttacker &&
               currentAttacker.active &&
               currentAttacker.instanceId == instanceId;
    }

    bool isFixedBlocked(int row, int col) const {
        // blocked_cell_a at row 2, col 3
        // blocked_cell_b at row 4, col 5
        return (row == 2 && col == 3) ||
               (row == 4 && col == 5);
    }


    bool isOccupiedByDynamicItem(int row, int col) const {
        for (const BoardItem& item : dynamicItems) {
            if (item.active && item.row == row && item.col == col) {
                return true;
            }
        }

        return false;
    }

    int findInactiveBoardItemSlot() {
        std::vector<int> inactiveEnemySlots;

        for (int i = 0; i < DYNAMIC_ITEM_COUNT; i++) {
            int instanceId = dynamicItemInstanceIds[i];

            if (!canSpawnAsEnemyInstance(instanceId)) {
                continue;
            }

            if (!dynamicItems[i].active) {
                inactiveEnemySlots.push_back(i);
            }
        }

        if (inactiveEnemySlots.empty()) {
            return -1;
        }

        std::uniform_int_distribution<int> slotDistribution(
            0,
            static_cast<int>(inactiveEnemySlots.size()) - 1
        );

        return inactiveEnemySlots[slotDistribution(randomEngine)];
    }


void spawnRandomBoardItem() {
    int slot = findInactiveBoardItemSlot();

    if (slot < 0) {
        statusMessage = "Board is full. Survive as long as possible.";
        return;
    }

    std::vector<glm::ivec2> freeCells;

    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            if (row == tokenRow && col == tokenCol) {
                continue;
            }

            if (isFixedBlocked(row, col)) {
                continue;
            }

            if (isOccupiedByDynamicItem(row, col)) {
                continue;
            }

            freeCells.push_back(glm::ivec2(row, col));
        }
    }

    if (freeCells.empty()) {
        statusMessage = "No free cells left.";
        return;
    }

    std::uniform_int_distribution<int> cellDistribution(
        0,
        static_cast<int>(freeCells.size()) - 1
    );

    glm::ivec2 chosenCell = freeCells[cellDistribution(randomEngine)];

        int instanceId = dynamicItemInstanceIds[slot];

        dynamicItems[slot].active = true;
        dynamicItems[slot].instanceId = instanceId;
        dynamicItems[slot].type = boardItemTypeForInstance(instanceId);
        dynamicItems[slot].row = chosenCell.x;
        dynamicItems[slot].col = chosenCell.y;

    std::ostringstream oss;
    oss << "Round " << roundNumber
        << ": spawned "
        << boardItemTypeName(dynamicItems[slot].type)
        << " at ("
        << dynamicItems[slot].row
        << ", "
        << dynamicItems[slot].col
        << "). Roll dice.";

    statusMessage = oss.str();

    std::cout << statusMessage << "\n";
}

    int signInt(int value) const {
    if (value > 0) {
        return 1;
    }

    if (value < 0) {
        return -1;
    }

    return 0;
}

// Checks whether a straight or diagonal line between two cells is unobstructed.
bool lineClearBetween(int fromRow, int fromCol, int toRow, int toCol) const {
    int dRow = signInt(toRow - fromRow);
    int dCol = signInt(toCol - fromCol);

    int row = fromRow + dRow;
    int col = fromCol + dCol;

    while (row != toRow || col != toCol) {
        if (isFixedBlocked(row, col)) {
            return false;
        }

        if (isOccupiedByDynamicItem(row, col)) {
            return false;
        }

        row += dRow;
        col += dCol;
    }

    return true;
}


bool boardItemAttacksPlayer(const BoardItem& item) const {
    if (!item.active) {
        return false;
    }

    if (item.type == BoardItemType::Obstacle) {
        return false;
    }

    int dRow = tokenRow - item.row;
    int dCol = tokenCol - item.col;

    int absRow = std::abs(dRow);
    int absCol = std::abs(dCol);

    switch (item.type) {
    case BoardItemType::Rook:
        if (dRow == 0 || dCol == 0) {
            return lineClearBetween(item.row, item.col, tokenRow, tokenCol);
        }
        return false;

    case BoardItemType::Bishop:
        if (absRow == absCol) {
            return lineClearBetween(item.row, item.col, tokenRow, tokenCol);
        }
        return false;

    case BoardItemType::Queen:
        if (dRow == 0 || dCol == 0 || absRow == absCol) {
            return lineClearBetween(item.row, item.col, tokenRow, tokenCol);
        }
        return false;

    case BoardItemType::Knight:
        return (absRow == 2 && absCol == 1) ||
               (absRow == 1 && absCol == 2);

    case BoardItemType::King:
        return (absRow <= 1 && absCol <= 1) &&
               !(absRow == 0 && absCol == 0);

    default:
        return false;
    }
}


bool anyBoardItemAttacksPlayer(BoardItem* attackingItem = nullptr) {
    for (BoardItem& item : dynamicItems) {
        if (boardItemAttacksPlayer(item)) {
            if (attackingItem != nullptr) {
                *attackingItem = item;
            }

            return true;
        }
    }

    return false;
}

void hideInactiveDynamicItems() {
    if (SC.TI == nullptr) {
        return;
    }

    for (const BoardItem& item : dynamicItems) {
        if (item.instanceId < 0) {
            continue;
        }

        if (item.instanceId >= SC.TI[0].InstanceCount) {
            continue;
        }

        if (!item.active) {
            SC.TI[0].I[item.instanceId].Wm =
                glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -20.0f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
        }
    }
}


void resetGame() {
    tokenRow = 6;
    tokenCol = 1;

    movementPoints = 0;
    diceRolling = false;
    die1Sleeping = true;
    die2Sleeping = true;

    die1Value = 1;
    die2Value = 1;

    roundNumber = 1;
    roundsSurvived = 0;
    gamePhase = GamePhase::WaitingForRoll;
    attackAnimationActive = false;
    attackAnimationTimer = 0.0f;
    hasCurrentAttacker = false;
    currentAttacker = BoardItem{};

    for (int i = 0; i < DYNAMIC_ITEM_COUNT; i++) {
        dynamicItems[i].active = false;
        dynamicItems[i].type = BoardItemType::Empty;
        dynamicItems[i].row = 0;
        dynamicItems[i].col = 0;
        dynamicItems[i].instanceId = dynamicItemInstanceIds[i];
    }

    hideInactiveDynamicItems();

    spawnRandomBoardItem();

    statusMessage = "New game. Round 1: roll dice.";
}

    void startAttackAnimation(const BoardItem& attacker) {
        currentAttacker = attacker;
        hasCurrentAttacker = true;

        attackAnimationActive = true;
        attackAnimationTimer = 0.0f;

        gamePhase = GamePhase::Attacking;
        movementPoints = 0;
        diceRolling = false;

        std::ostringstream oss;
        oss << boardItemTypeName(attacker.type)
            << " attacks!";

        statusMessage = oss.str();

        std::cout << statusMessage << "\n";
    }


    void finishAttackAnimation() {
        attackAnimationActive = false;
        attackAnimationTimer = ATTACK_ANIMATION_DURATION;

        gamePhase = GamePhase::GameOver;
        movementPoints = 0;
        diceRolling = false;

        if (roundsSurvived > highScore) {
            highScore = roundsSurvived;
            saveHighScore();
        }

        std::ostringstream oss;
        oss << "GAME OVER! "
            << boardItemTypeName(currentAttacker.type)
            << " attacked from ("
            << currentAttacker.row
            << ", "
            << currentAttacker.col
            << "). Press R to restart.";

        statusMessage = oss.str();

        std::cout << statusMessage << "\n";
    }


void finishPlayerMovement() {
    BoardItem attacker{};

    if (anyBoardItemAttacksPlayer(&attacker)) {
        startAttackAnimation(attacker);
        return;
    }

    roundsSurvived++;
    roundNumber++;

    gamePhase = GamePhase::WaitingForRoll;
    movementPoints = 0;

    spawnRandomBoardItem();
}
    bool isBlocked(int row, int col) const {
        return isFixedBlocked(row, col) ||
               isOccupiedByDynamicItem(row, col);
    }
    void updateAttackAnimation(float deltaT) {
        if (gamePhase != GamePhase::Attacking) {
            return;
        }

        attackAnimationTimer += deltaT;

        if (attackAnimationTimer >= ATTACK_ANIMATION_DURATION) {
            finishAttackAnimation();
        }
    }

    // Main per-frame gameplay and camera update.
    float GameLogic() {
        const float FOVy = glm::radians(45.0f);
        const float nearPlane = 0.1f;
        const float farPlane = 100.0f;

        float deltaT;
        glm::vec3 m = glm::vec3(0.0f);
        glm::vec3 r = glm::vec3(0.0f);
        bool fire = false;

        getSixAxis(deltaT, m, r, fire);
        updateAttackAnimation(deltaT);
        if (
            gamePhase == GamePhase::GameOver &&
            glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS
        ) {
            resetGame();
        }
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
        if (gamePhase == GamePhase::Moving && moveCooldown <= 0.0f) {
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
            gamePhase == GamePhase::WaitingForRoll &&
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

        return glm::vec3(x, 0.25f, z);
    }


    bool isInsideBoard(int row, int col) const {
        return row >= 0 && row < GRID_ROWS &&
               col >= 0 && col < GRID_COLS;
    }


    void tryMoveToken(int dRow, int dCol) {
        if (gamePhase != GamePhase::Moving) {
            std::cout << "Roll dice first.\n";
            return;
        }

        if (movementPoints <= 0) {
            std::cout << "No movement points left.\n";
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
                << "Blocked: occupied cell ("
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

        if (movementPoints <= 0) {
            finishPlayerMovement();
        }
    }


    // -------------------------------
    // Model matrices
    // -------------------------------

    // Converts the token's grid position into world position and applies the token scale.
    glm::mat4 tokenModelMatrix() const {
        glm::vec3 pos = gridToWorld(tokenRow, tokenCol);

        return glm::translate(glm::mat4(1.0f), pos) *
               glm::scale(glm::mat4(1.0f), glm::vec3(0.03f, 0.03f, 0.03f));
    }

    std::vector<glm::vec2> pipOffsetsForValue(int value) const {
        float o = 0.105f;

        switch (value) {
        case 1:
            return {
                glm::vec2(0.0f, 0.0f)
            };

        case 2:
            return {
                glm::vec2(-o, -o),
                glm::vec2( o,  o)
            };

        case 3:
            return {
                glm::vec2(-o, -o),
                glm::vec2(0.0f, 0.0f),
                glm::vec2( o,  o)
            };

        case 4:
            return {
                glm::vec2(-o, -o),
                glm::vec2( o, -o),
                glm::vec2(-o,  o),
                glm::vec2( o,  o)
            };

        case 5:
            return {
                glm::vec2(-o, -o),
                glm::vec2( o, -o),
                glm::vec2(0.0f, 0.0f),
                glm::vec2(-o,  o),
                glm::vec2( o,  o)
            };

        case 6:
        default:
            return {
                glm::vec2(-o, -o),
                glm::vec2( o, -o),
                glm::vec2(-o, 0.0f),
                glm::vec2( o, 0.0f),
                glm::vec2(-o,  o),
                glm::vec2( o,  o)
            };
        }
    }

    // Applies translation from physics position, rotations from dice spin, and final scale.
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

    // Places it on its board cell and, if it is attacking, adds lunge/jump/scale animation.
    glm::mat4 boardItemModelMatrix(const BoardItem& item) const {
        glm::vec3 basePos = gridToWorld(item.row, item.col);
        glm::vec3 finalPos = basePos;

        if (
            gamePhase == GamePhase::Attacking &&
            hasCurrentAttacker &&
            item.instanceId == currentAttacker.instanceId
        ) {
            float t = attackAnimationTimer / ATTACK_ANIMATION_DURATION;
            t = std::clamp(t, 0.0f, 1.0f);

            // Smoothstep easing: slow start, fast middle, slow end.
            float easedT = t * t * (3.0f - 2.0f * t);

            glm::vec3 playerPos = gridToWorld(tokenRow, tokenCol);

            // Move only partway toward the player, so the models do not fully overlap.
            glm::vec3 lungeTarget = basePos + (playerPos - basePos) * 0.65f;

            finalPos = glm::mix(basePos, lungeTarget, easedT);

            // Small hop arc during the attack.
            float jumpHeight = std::sin(t * 3.14159265f) * 0.45f;
            finalPos.y += jumpHeight;
        }

        glm::mat4 M = glm::mat4(1.0f);

        M = glm::translate(M, finalPos);

        // Most chess GLTF pieces in the scene were originally rotated -90 degrees.
        M = glm::rotate(M, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        // Slight scale pulse during attack.

        float scale = 0.13f;


        M = glm::scale(M, glm::vec3(scale, scale, scale));

        return M;
    }

    bool isDicePipInstance(int instanceId) const {
        return instanceId >= DIE_1_PIP_START_INDEX &&
               instanceId <= LAST_DICE_PIP_INDEX;
    }

    // Used to keep inactive preloaded objects from appearing or casting visible shadows.
    glm::mat4 hiddenModelMatrix() const {
        return glm::translate(
                   glm::mat4(1.0f),
                   glm::vec3(1000.0f, -1000.0f, 1000.0f)
               ) *
               glm::scale(
                   glm::mat4(1.0f),
                   glm::vec3(0.0001f)
               );
    }

    // Active items are placed on the board; inactive items are hidden.
    void updateDynamicBoardItemInstances() {
        if (SC.TI == nullptr) {
            return;
        }

        for (const BoardItem& item : dynamicItems) {
            if (item.instanceId < 0) {
                continue;
            }

            if (item.instanceId >= SC.TI[0].InstanceCount) {
                continue;
            }

            if (item.active) {
                SC.TI[0].I[item.instanceId].Wm = boardItemModelMatrix(item);
            } else {
                SC.TI[0].I[item.instanceId].Wm =
                    glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -20.0f, 0.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
            }
        }
    }

    // This acts like a camera from the light's position and is used to render/check that light's shadow map.
    glm::mat4 computeLightViewProj(int lightIndex) const {
        glm::vec3 lightPos = glm::vec3(cornerLightPositions[lightIndex]);

        // All lamps aim at the same fixed point at board center.
        glm::vec3 target = glm::vec3(0.0f, 0.35f, 0.0f);

        glm::mat4 lightView = glm::lookAt(
            lightPos,
            target,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        glm::mat4 lightProj = glm::perspectiveRH_ZO(
            glm::radians(82.0f),
            1.0f,
            0.20f,
            16.0f
        );


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

    // Chooses the per-object material/tint sent to Arena.frag.
    // Used for enemy colors, attacker highlight, dice/table/tray colors, and emissive lamp bulbs.
    glm::vec4 objectMaterialColor(int instanceId) const {
        if (isDicePipInstance(instanceId)) {
            return glm::vec4(0.01f, 0.01f, 0.01f, 1.0f);
        }
        if (isDynamicItemInstance(instanceId)) {
            if (isCurrentAttackerInstance(instanceId)) {
                if (
                    gamePhase == GamePhase::Attacking ||
                    gamePhase == GamePhase::GameOver
                ) {
                    // Alpha > 1.5 means emissive in Arena.frag.
                    return glm::vec4(1.0f, 0.05f, 0.02f, 2.0f);
                }
            }

            BoardItemType type = dynamicItemTypeFromInstance(instanceId);

            switch (type) {
            case BoardItemType::Obstacle:
                return glm::vec4(0.28f, 0.16f, 0.06f, 0.85f);

            case BoardItemType::Rook:
                return glm::vec4(0.75f, 0.75f, 0.78f, 1.0f);

            case BoardItemType::Bishop:
                return glm::vec4(0.55f, 0.80f, 1.00f, 1.0f);

            case BoardItemType::Knight:
                return glm::vec4(1.00f, 0.65f, 0.20f, 1.0f);

            case BoardItemType::Queen:
                return glm::vec4(1.00f, 0.15f, 0.15f, 1.0f);

            case BoardItemType::King:
                return glm::vec4(0.15f, 0.15f, 0.15f, 1.0f);

            default:
                return glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            }
        }
        switch (instanceId) {
        case 0:
            // table_surface
            return glm::vec4(0.45f, 0.25f, 0.10f, 0.25f);

        case 2:
            // player_token
            return glm::vec4(0.25f, 0.25f, 1.00f, 1.0f);

        case 3:
        case 4:
            // blocked cells / obstacles
            return glm::vec4(0.25f, 0.25f, 0.28f, 0.25f);

        case 5:
        case 6:
            // dice
            return glm::vec4(0.95f, 0.95f, 0.90f, 0.25f);


        case LAMP_1_BULB_INSTANCE_INDEX:
            return lampBulbMaterial(0);

        case LAMP_2_BULB_INSTANCE_INDEX:
            return lampBulbMaterial(1);

        case LAMP_3_BULB_INSTANCE_INDEX:
            return lampBulbMaterial(2);

        case LAMP_4_BULB_INSTANCE_INDEX:
            return lampBulbMaterial(3);
        case DICE_TRAY_INDEX:
            // dice tray
            return glm::vec4(0.32f, 0.18f, 0.08f, 0.55f);

        default:
            return glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        }
    }

    // Applies the current dice model matrices to the loaded scene instances.
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

    void updateDicePipInstances() {
        if (SC.TI == nullptr) {
            return;
        }

        if (SC.TI[0].InstanceCount <= LAST_DICE_PIP_INDEX) {
            return;
        }

        for (int instanceId = DIE_1_PIP_START_INDEX;
             instanceId <= LAST_DICE_PIP_INDEX;
             instanceId++) {
            SC.TI[0].I[instanceId].Wm = hiddenModelMatrix();
        }
    }

    // Applies the current player-token model matrix to the loaded scene instance.
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