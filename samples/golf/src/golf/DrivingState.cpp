/*-----------------------------------------------------------------------

Matt Marchant 2021 - 2022
http://trederia.blogspot.com

Super Video Golf - zlib licence.

This software is provided 'as-is', without any express or
implied warranty.In no event will the authors be held
liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute
it freely, subject to the following restrictions :

1. The origin of this software must not be misrepresented;
you must not claim that you wrote the original software.
If you use this software in a product, an acknowledgment
in the product documentation would be appreciated but
is not required.

2. Altered source versions must be plainly marked as such,
and must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any
source distribution.

-----------------------------------------------------------------------*/

#include "DrivingState.hpp"
#include "PoissonDisk.hpp"
#include "SharedStateData.hpp"
#include "CommandIDs.hpp"
#include "MenuConsts.hpp"
#include "GameConsts.hpp"
#include "FpsCameraSystem.hpp"
#include "TextAnimCallback.hpp"
#include "DrivingRangeDirector.hpp"
#include "BallSystem.hpp"
#include "MessageIDs.hpp"
#include "Clubs.hpp"
#include "PlayerColours.hpp"
#include "GolfParticleDirector.hpp"
#include "GolfSoundDirector.hpp"
#include "CameraFollowSystem.hpp"
#include "ClientCollisionSystem.hpp"
#include "FloatingTextSystem.hpp"
#include "CloudSystem.hpp"
#include "PoissonDisk.hpp"
#include "BeaconCallback.hpp"
#include "server/ServerMessages.hpp"
#include "../GolfGame.hpp"
#include "../ErrorCheck.hpp"

#include <Achievements.hpp>
#include <Social.hpp>

#include <crogine/audio/AudioMixer.hpp>
#include <crogine/core/ConfigFile.hpp>
#include <crogine/core/FileSystem.hpp>
#include <crogine/gui/Gui.hpp>
#include <crogine/ecs/components/Callback.hpp>
#include <crogine/ecs/components/UIInput.hpp>
#include <crogine/ecs/components/Transform.hpp>
#include <crogine/ecs/components/Sprite.hpp>
#include <crogine/ecs/components/SpriteAnimation.hpp>
#include <crogine/ecs/components/Text.hpp>
#include <crogine/ecs/components/Drawable2D.hpp>
#include <crogine/ecs/components/Model.hpp>
#include <crogine/ecs/components/Camera.hpp>
#include <crogine/ecs/components/AudioListener.hpp>
#include <crogine/ecs/components/CommandTarget.hpp>

#include <crogine/ecs/systems/CallbackSystem.hpp>
#include <crogine/ecs/systems/UISystem.hpp>
#include <crogine/ecs/systems/BillboardSystem.hpp>
#include <crogine/ecs/systems/SpriteSystem2D.hpp>
#include <crogine/ecs/systems/SpriteSystem3D.hpp>
#include <crogine/ecs/systems/SpriteAnimator.hpp>
#include <crogine/ecs/systems/SkeletalAnimator.hpp>
#include <crogine/ecs/systems/TextSystem.hpp>
#include <crogine/ecs/systems/CameraSystem.hpp>
#include <crogine/ecs/systems/RenderSystem2D.hpp>
#include <crogine/ecs/systems/ShadowMapRenderer.hpp>
#include <crogine/ecs/systems/ModelRenderer.hpp>
#include <crogine/ecs/systems/ParticleSystem.hpp>
#include <crogine/ecs/systems/AudioSystem.hpp>
#include <crogine/ecs/systems/AudioPlayerSystem.hpp>

#include <crogine/graphics/SpriteSheet.hpp>
#include <crogine/graphics/DynamicMeshBuilder.hpp>
#include <crogine/util/Constants.hpp>
#include <crogine/util/Random.hpp>
#include <crogine/util/Maths.hpp>
#include <crogine/detail/glm/gtc/matrix_transform.hpp>

namespace
{
#include "CelShader.inl"
#include "TerrainShader.inl"
#include "TransitionShader.inl"
#include "MinimapShader.inl"
#include "WireframeShader.inl"
#include "BillboardShader.inl"
#include "CloudShader.inl"
#include "BeaconShader.inl"
#include "WaterShader.inl"
#include "ShaderIncludes.inl"

#ifdef CRO_DEBUG_
    std::int32_t debugFlags = 0;
    bool useFreeCam = false;
    std::array<glm::mat4, 5u> camTx = {};
    std::size_t camIdx = 0;
    cro::Entity ballEntity;
    float dotProd = 0.f;
#define DEBUG_DRAW true
#else
#define DEBUG_DRAW false
#endif

    float playerXScale = 1.f;

    static constexpr glm::vec3 CameraPosition = PlayerPosition + glm::vec3(0.f, CameraStrokeHeight, CameraStrokeOffset);

    static constexpr glm::vec2 BillboardChunk(40.f, 50.f);
    static constexpr std::size_t ChunkCount = 5;

    struct FoliageCallback final
    {
        FoliageCallback(float d = 0.f) : delay(d + 8.f) {} //magic number is some delay before effect starts
        float delay = 0.f;
        float progress = 0.f;
        static constexpr float Distance = 14.f;

        void operator() (cro::Entity e, float dt)
        {
            delay -= (dt * 1.6f); 

            if (delay < 0)
            {
                progress = std::min(1.f, progress + dt);

                auto pos = e.getComponent<cro::Transform>().getPosition();
                pos.y = (cro::Util::Easing::easeInOutQuint(progress) - 1.f) * Distance;
                e.getComponent<cro::Transform>().setPosition(pos);

                if (progress == 1)
                {
                    e.getComponent<cro::Callback>().active = false;
                }
            }
        }
    };

    struct FlagCallbackData final
    {
        float progress = 1.f;
        enum
        {
            Out, In
        }state = Out;
        glm::vec3 startPos = glm::vec3(0.f);
        glm::vec3 targetPos = glm::vec3(0.f);
        static constexpr float MaxDepth = 3.f;
    };
}

DrivingState::DrivingState(cro::StateStack& stack, cro::State::Context context, SharedStateData& sd)
    : cro::State        (stack, context),
    m_sharedData        (sd),
    m_inputParser       (sd, context.appInstance.getMessageBus()),
    m_gameScene         (context.appInstance.getMessageBus()),
    m_skyScene          (context.appInstance.getMessageBus()),
    m_uiScene           (context.appInstance.getMessageBus(), 512),
    m_viewScale         (1.f),
    m_scaleBuffer       ("PixelScale"),
    m_resolutionBuffer  ("ScaledResolution"),
    m_windBuffer        ("WindValues"),
    m_targetIndex       (0),
    m_strokeCountIndex  (0),
    m_currentCamera     (CameraID::Player)
{
    std::fill(m_topScores.begin(), m_topScores.end(), 0.f);
    loadScores();   
    
    context.mainWindow.loadResources([this]() {
        addSystems();
        loadAssets();
        createScene();
    });

    Achievements::setActive(true);

    Social::setStatus(Social::InfoID::Menu, { "On The Driving Range" });

#ifdef CRO_DEBUG_
    registerWindow([&]()
        {
            if (ImGui::Begin("Window"))
            {
                static float maxDist = 80.f;
                if (ImGui::SliderFloat("Distance", &maxDist, 1.f, 80.f))
                {
                    m_gameScene.getActiveCamera().getComponent<cro::Camera>().setMaxShadowDistance(maxDist);
                }

                static float overshoot = 0.f;
                if (ImGui::SliderFloat("Overshoot", &overshoot, 0.f, 20.f))
                {
                    m_gameScene.getActiveCamera().getComponent<cro::Camera>().setShadowExpansion(overshoot);
                }

                //ImGui::Image(m_cameras[CameraID::Player].getComponent<cro::Camera>().shadowMapBuffer.getTexture(), { 256.f, 256.f }, { 0.f, 1.f }, { 1.f, 0.f });
            }
            ImGui::End();
        });
#endif
}

//public
bool DrivingState::handleEvent(const cro::Event& evt)
{
    if (cro::ui::wantsMouse() || cro::ui::wantsKeyboard())
    {
        return true;
    }

#ifdef USE_GNS
    const auto closeLeaderboard = [&]()
    {
        auto* uiSystem = m_uiScene.getSystem<cro::UISystem>();
        if (uiSystem->getActiveGroup() == MenuID::Leaderboard)
        {
            uiSystem->setActiveGroup(MenuID::Dummy);
            m_leaderboardEntity.getComponent<cro::Callback>().active = true;

            m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
        }
    };
#endif

    if (evt.type == SDL_KEYUP)
    {
        cro::App::getWindow().setMouseCaptured(true);
        switch (evt.key.keysym.sym)
        {
        default: break;
        case SDLK_p:
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
        case SDLK_PAUSE:
            requestStackPush(StateID::Pause);
            break;
            //make sure system buttons don't do anything
        case SDLK_F1:
        case SDLK_F5:

            break;
#ifdef CRO_DEBUG_
        case SDLK_F7:
            floatingMessage("buns");
            break;
        case SDLK_HOME:
            debugFlags = (debugFlags == 0) ? BulletDebug::DebugFlags : 0;
            m_gameScene.getSystem<BallSystem>()->setDebugFlags(debugFlags);
            break;
        case SDLK_END:
            saveScores();
            break;
        case SDLK_INSERT:
            toggleFreeCam();
            break;
        case SDLK_RIGHT:
        /*{
            camIdx = (camIdx + 1) % camTx.size();
            m_cameras[CameraID::Player].getComponent<cro::Transform>().setLocalTransform(camTx[camIdx]);
        }*/
            break;
        case SDLK_PAGEDOWN:
        {
            cro::Command cmd;
            cmd.targetFlags = CommandID::Hole;
            cmd.action = [&](cro::Entity e, float)
            {
                e.getComponent<cro::Callback>().getUserData<FlagCallbackData>().targetPos = m_holeData[0].pin;
                e.getComponent<cro::Callback>().active = true;
            };
            m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
        }
        break;
        case SDLK_PAGEUP:
        {
            m_summaryScreen.root.getComponent<cro::Callback>().active = true;
            for (auto e : m_summaryScreen.stars)
            {
                e.getComponent<cro::Callback>().active = true;
            }
            /*cro::Command cmd;
            cmd.targetFlags = CommandID::UI::DrivingBoard;
            cmd.action = [](cro::Entity e, float) {e.getComponent<cro::Callback>().active = true; };
            m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);*/
        }
        break;
        case SDLK_KP_MULTIPLY:

            break;
#endif
        }
    }
    else if (evt.type == SDL_KEYDOWN)
    {
        switch (evt.key.keysym.sym)
        {
        default: break;
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_LEFT:
        case SDLK_RIGHT:
            cro::App::getWindow().setMouseCaptured(true);
            break;
        }
    }
    else if (evt.type == SDL_CONTROLLERAXISMOTION)
    {
        if (evt.caxis.value > LeftThumbDeadZone)
        {
            cro::App::getWindow().setMouseCaptured(true);
        }
    }
    else if (evt.type == SDL_CONTROLLERBUTTONUP)
    {
        switch (evt.cbutton.button)
        {
        default: break;
        case cro::GameController::ButtonStart:
        case cro::GameController::ButtonGuide:
            requestStackPush(StateID::Pause);
            break;
#ifdef USE_GNS
        case cro::GameController::ButtonB:
            closeLeaderboard();
            break;
#endif
        }
    }
    else if (evt.type == SDL_MOUSEMOTION)
    {
#ifdef CRO_DEBUG_
        if (!useFreeCam) {
#endif
            if ((evt.motion.state & SDL_BUTTON_RMASK) == 0)
            {
                cro::App::getWindow().setMouseCaptured(false);
            }
#ifdef CRO_DEBUG_
        }
#endif // CRO_DEBUG_

    }
    else if (evt.type == SDL_CONTROLLERDEVICEREMOVED)
    {
        requestStackPush(StateID::Pause);
    }
#ifdef CRO_DEBUG_
    m_gameScene.getSystem<FpsCameraSystem>()->handleEvent(evt);
#endif

    m_uiScene.getSystem<cro::UISystem>()->handleEvent(evt);
    m_inputParser.handleEvent(evt);
    m_gameScene.forwardEvent(evt);
    m_skyScene.forwardEvent(evt);
    m_uiScene.forwardEvent(evt);
    return true;
}

void DrivingState::handleMessage(const cro::Message& msg)
{
    //director must handle message first so score is
    //up to date by the time the switchblock below is
    //processed
    m_gameScene.forwardMessage(msg);
    m_skyScene.forwardMessage(msg);
    m_uiScene.forwardMessage(msg);

    switch (msg.id)
    {
    default: break;
    case Social::MessageID::SocialMessage:
    {
        const auto& data = msg.getData<Social::SocialEvent>();
        if (data.type == Social::SocialEvent::XPAwarded)
        {
            floatingMessage(std::to_string(data.level) + " XP");
        }
    }
    break;
    case MessageID::CollisionMessage:
    {
        const auto& data = msg.getData<CollisionEvent>();
        if (data.terrain == TerrainID::Scrub)
        {
            if (cro::Util::Random::value(0, 2) == 0)
            {
                auto* msg2 = cro::App::getInstance().getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
                msg2->type = GolfEvent::BirdHit;
                msg2->position = data.position;
                auto dir = data.position - m_cameras[m_currentCamera].getComponent<cro::Transform>().getPosition();
                msg2->travelDistance = std::atan2(dir.z, dir.x);
            }
        }
    }
        break;
    case cro::Message::SkeletalAnimationMessage:
    {
        const auto& data = msg.getData<cro::Message::SkeletalAnimationEvent>();
        if (data.userType == SpriteAnimID::Swing)
        {
            hitBall();

            //enable the camera following
            m_gameScene.setSystemActive<CameraFollowSystem>(true);
        }
    }
    break;
    case sv::MessageID::GolfMessage:
    {
        const auto& data = msg.getData<GolfBallEvent>();
        if (data.type == GolfBallEvent::TurnEnded)
        {
            //display a message with score
            showMessage(glm::length(PlayerPosition - data.position));
        }
    }
        break;
    case MessageID::GolfMessage:
    {
        const auto& data = msg.getData<GolfEvent>();
        switch (data.type)
        {
        default: break;
        case GolfEvent::HitBall:
        {
            //animation event triggers actual ball hit
            cro::Command cmd;
            cmd.targetFlags = CommandID::PlayerAvatar;
            cmd.action = [&](cro::Entity e, float)
            {
                e.getComponent<cro::Skeleton>().play(m_avatar.animationIDs[AnimationID::Swing]);
            };
            m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

            m_inputParser.setActive(false);
        }
            break;
        case GolfEvent::ClubChanged:
        {
            cro::Command cmd;
            cmd.targetFlags = CommandID::StrokeIndicator;
            cmd.action = [&](cro::Entity e, float)
            {
                float scale = Clubs[m_inputParser.getClub()].power / Clubs[ClubID::Driver].power;
                e.getComponent<cro::Transform>().setScale({ scale, 1.f });
            };
            m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

            //update club text colour based on distance
            cmd.targetFlags = CommandID::UI::ClubName;
            cmd.action = [&](cro::Entity e, float)
            {
                e.getComponent<cro::Text>().setString(Clubs[m_inputParser.getClub()].getName(m_sharedData.imperialMeasurements));

                auto dist = glm::length(PlayerPosition - m_holeData[m_gameScene.getDirector<DrivingRangeDirector>()->getCurrentHole()].pin) * 1.67f;
                if (m_inputParser.getClub() < ClubID::NineIron &&
                    Clubs[m_inputParser.getClub()].target > dist)
                {
                    e.getComponent<cro::Text>().setFillColour(TextHighlightColour);
                }
                else
                {
                    e.getComponent<cro::Text>().setFillColour(TextNormalColour);
                }
            };
            m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);


            //set the correct club model on our attachment
            if (m_avatar.handsAttachment)
            {
                //TODO handle cases when club model failed to load...
                if (m_inputParser.getClub() < ClubID::FiveIron)
                {
                    m_clubModels[ClubModel::Iron].getComponent<cro::Model>().setHidden(true);
                    m_clubModels[ClubModel::Wood].getComponent<cro::Model>().setHidden(false);

                    m_avatar.handsAttachment->setModel(m_clubModels[ClubModel::Wood]);
                }
                else
                {
                    m_clubModels[ClubModel::Iron].getComponent<cro::Model>().setHidden(false);
                    m_clubModels[ClubModel::Wood].getComponent<cro::Model>().setHidden(true);

                    m_avatar.handsAttachment->setModel(m_clubModels[ClubModel::Iron]);
                }
            }
        }
        break;
        }
    }
        break;
    case MessageID::SceneMessage:
    {
        const auto& data = msg.getData<SceneEvent>();
        switch (data.type)
        {
        default: break;
        case SceneEvent::TransitionComplete:
        {
            m_gameScene.getSystem<CameraFollowSystem>()->resetCamera();
        }
        break;
        case SceneEvent::RequestSwitchCamera:
            setActiveCamera(data.data);
            break;
        }
    }
    break;
    case cro::Message::StateMessage:
    {
        const auto& data = msg.getData<cro::Message::StateEvent>();
        if (data.action == cro::Message::StateEvent::Popped)
        {
            if (data.id == StateID::Options)
            {
                //update the beacon if settings changed
                cro::Command cmd;
                cmd.targetFlags = CommandID::Beacon;
                cmd.action = [&](cro::Entity e, float)
                {
                    e.getComponent<cro::Model>().setHidden(!m_sharedData.showBeacon);
                    e.getComponent<cro::Model>().setMaterialProperty(0, "u_colourRotation", m_sharedData.beaconColour);
                };
                m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

                //and the measurement settings
                cmd.targetFlags = CommandID::UI::ClubName;
                cmd.action = [&](cro::Entity e, float)
                {
                    e.getComponent<cro::Text>().setString(Clubs[m_inputParser.getClub()].getName(m_sharedData.imperialMeasurements));
                };
                m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

                //update distance to hole
                cmd.targetFlags = CommandID::UI::PinDistance;
                cmd.action = [&](cro::Entity e, float)
                {
                    float ballDist = 
                        glm::length(PlayerPosition - m_holeData[m_gameScene.getDirector<DrivingRangeDirector>()->getCurrentHole()].pin);
                    formatDistanceString(ballDist, e.getComponent<cro::Text>(), m_sharedData.imperialMeasurements);

                    auto bounds = cro::Text::getLocalBounds(e);
                    bounds.width = std::floor(bounds.width / 2.f);
                    e.getComponent<cro::Transform>().setOrigin({ bounds.width, 0.f });
                };
                m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
            }
        }
    }
        break;
    }
}

bool DrivingState::simulate(float dt)
{
    auto windDir = m_gameScene.getSystem<BallSystem>()->getWindDirection();
    updateWindDisplay(windDir);

    static float elapsed = 0.f;
    elapsed += dt;

    m_windUpdate.currentWindSpeed += (windDir.y - m_windUpdate.currentWindSpeed) * dt;
    m_windUpdate.currentWindVector += (windDir - m_windUpdate.currentWindVector) * dt;

    WindData data;
    data.direction[0] = m_windUpdate.currentWindVector.x;
    data.direction[1] = m_windUpdate.currentWindSpeed;
    data.direction[2] = m_windUpdate.currentWindVector.z;
    data.elapsedTime = elapsed;
    m_windBuffer.setData(data);

    m_inputParser.update(dt, TerrainID::Fairway);
    m_gameScene.simulate(dt);
    m_uiScene.simulate(dt);


    const auto& srcCam = m_gameScene.getActiveCamera().getComponent<cro::Camera>();
    auto& dstCam = m_skyScene.getActiveCamera().getComponent<cro::Camera>();

    dstCam.viewport = srcCam.viewport;
    dstCam.setPerspective(srcCam.getFOV(), srcCam.getAspectRatio(), 1.f, 14.f);

    m_skyScene.getActiveCamera().getComponent<cro::Transform>().setRotation(m_gameScene.getActiveCamera().getComponent<cro::Transform>().getWorldRotation());
    auto pos = m_gameScene.getActiveCamera().getComponent<cro::Transform>().getWorldPosition();
    pos.x = 0.f;
    pos.y /= 64.f;
    pos.z = 0.f;
    m_skyScene.getActiveCamera().getComponent<cro::Transform>().setPosition(pos);
    m_skyScene.simulate(dt);

    return true;
}

void DrivingState::render()
{
    //TODO these probably only need to be bound once on start-up
    m_scaleBuffer.bind(0);
    m_resolutionBuffer.bind(1);
    m_windBuffer.bind(2);

    m_backgroundTexture.clear();
    m_skyScene.render();
    glClear(GL_DEPTH_BUFFER_BIT);
    m_gameScene.render();
#ifdef CRO_DEBUG_
    auto& cam = m_gameScene.getActiveCamera().getComponent<cro::Camera>();
    m_gameScene.getSystem<BallSystem>()->renderDebug(cam.getActivePass().viewProjectionMatrix, m_backgroundTexture.getSize());
#endif
    m_backgroundTexture.display();

    m_uiScene.render();
}

//private
void DrivingState::toggleFreeCam()
{
#ifdef CRO_DEBUG_
    useFreeCam = !useFreeCam;
    if (useFreeCam)
    {
        m_defaultCam = m_gameScene.setActiveCamera(m_freeCam);
        m_gameScene.setActiveListener(m_freeCam);
    }
    else
    {
        m_gameScene.setActiveCamera(m_defaultCam);
        m_gameScene.setActiveListener(m_defaultCam);
    }

    m_gameScene.setSystemActive<FpsCameraSystem>(useFreeCam);
    m_inputParser.setActive(!useFreeCam);
    cro::App::getWindow().setMouseCaptured(useFreeCam);
#endif
}

void DrivingState::addSystems()
{
    auto& mb = getContext().appInstance.getMessageBus();

    m_gameScene.addSystem<cro::CommandSystem>(mb);
    m_gameScene.addSystem<cro::CallbackSystem>(mb);
    m_gameScene.addSystem<BallSystem>(mb, DEBUG_DRAW);
    m_gameScene.addSystem<cro::SkeletalAnimator>(mb);
    m_gameScene.addSystem<cro::BillboardSystem>(mb);
    m_gameScene.addSystem<cro::SpriteSystem3D>(mb, PixelPerMetre);
    m_gameScene.addSystem<CloudSystem>(mb);
    m_gameScene.addSystem<CameraFollowSystem>(mb);
    m_gameScene.addSystem<cro::CameraSystem>(mb);
    m_gameScene.addSystem<cro::ShadowMapRenderer>(mb);
    //m_gameScene.getSystem<cro::ShadowMapRenderer>()->setNumCascades(1);
    m_gameScene.addSystem<cro::ModelRenderer>(mb);
    m_gameScene.addSystem<cro::ParticleSystem>(mb);
    m_gameScene.addSystem<cro::AudioSystem>(mb);
#ifdef  CRO_DEBUG_
    m_gameScene.addSystem<FpsCameraSystem>(mb);

    m_gameScene.setSystemActive<FpsCameraSystem>(false);
#endif

    m_gameScene.setSystemActive<CameraFollowSystem>(false);

    m_gameScene.addDirector<DrivingRangeDirector>(m_holeData);
    m_gameScene.addDirector<GolfSoundDirector>(m_resources.audio);
    m_gameScene.addDirector<GolfParticleDirector>(m_resources.textures);


    m_skyScene.addSystem<cro::CameraSystem>(mb);
    m_skyScene.addSystem<cro::ModelRenderer>(mb);


    m_uiScene.addSystem<cro::CommandSystem>(mb);
    m_uiScene.addSystem<cro::CallbackSystem>(mb);
    m_uiScene.addSystem<FloatingTextSystem>(mb);
    m_uiScene.addSystem<cro::UISystem>(mb);
    m_uiScene.addSystem<cro::SpriteAnimator>(mb);
    m_uiScene.addSystem<cro::SpriteSystem2D>(mb);
    m_uiScene.addSystem<cro::TextSystem>(mb);
    m_uiScene.addSystem<cro::CameraSystem>(mb);
    m_uiScene.addSystem<cro::RenderSystem2D>(mb);
    m_uiScene.addSystem<cro::ParticleSystem>(mb);
    m_uiScene.addSystem<cro::AudioPlayerSystem>(mb);
}

void DrivingState::loadAssets()
{
    std::string wobble;
    if (m_sharedData.vertexSnap)
    {
        wobble = "#define WOBBLE\n";
    }

    for (const auto& [name, str] : IncludeMappings)
    {
        m_resources.shaders.addInclude(name, str);
    }

    //models
    m_resources.shaders.loadFromString(ShaderID::Cel, CelVertexShader, CelFragmentShader, "#define VERTEX_COLOURED\n" + wobble);
    m_resources.shaders.loadFromString(ShaderID::CelTextured, CelVertexShader, CelFragmentShader, "#define TEXTURED\n" + wobble);
    m_resources.shaders.loadFromString(ShaderID::CelTexturedSkinned, CelVertexShader, CelFragmentShader, "#define FADE_INPUT\n#define TEXTURED\n#define SKINNED\n#define NOCHEX\n" + wobble);
    m_resources.shaders.loadFromString(ShaderID::Course, CelVertexShader, CelFragmentShader, "#define TEXTURED\n#define RX_SHADOWS\n" + wobble);
    m_resources.shaders.loadFromString(ShaderID::Hair, CelVertexShader, CelFragmentShader, "#define FADE_INPUT\n#define USER_COLOUR\n#define NOCHEX\n#define RX_SHADOWS\n" + wobble);
    m_resources.shaders.loadFromString(ShaderID::Billboard, BillboardVertexShader, BillboardFragmentShader);

    //scanline transition
    m_resources.shaders.loadFromString(ShaderID::Transition, MinimapVertex, ScanlineTransition);

    //materials
    auto* shader = &m_resources.shaders.get(ShaderID::Cel);
    m_scaleBuffer.addShader(*shader);
    m_resolutionBuffer.addShader(*shader);
    m_materialIDs[MaterialID::Cel] = m_resources.materials.add(*shader);
    
    shader = &m_resources.shaders.get(ShaderID::CelTextured);
    m_scaleBuffer.addShader(*shader);
    m_resolutionBuffer.addShader(*shader);
    m_materialIDs[MaterialID::CelTextured] = m_resources.materials.add(*shader);
   
    shader = &m_resources.shaders.get(ShaderID::CelTexturedSkinned);
    m_resolutionBuffer.addShader(*shader);
    m_materialIDs[MaterialID::CelTexturedSkinned] = m_resources.materials.add(*shader);

    shader = &m_resources.shaders.get(ShaderID::Hair);
    m_materialIDs[MaterialID::Hair] = m_resources.materials.add(*shader);
    m_resolutionBuffer.addShader(*shader);

    shader = &m_resources.shaders.get(ShaderID::Course);
    m_scaleBuffer.addShader(*shader);
    m_resolutionBuffer.addShader(*shader);
    m_materialIDs[MaterialID::Course] = m_resources.materials.add(*shader);

    shader = &m_resources.shaders.get(ShaderID::Billboard);
    m_scaleBuffer.addShader(*shader);
    m_resolutionBuffer.addShader(*shader);
    m_windBuffer.addShader(*shader);
    m_materialIDs[MaterialID::Billboard] = m_resources.materials.add(*shader);

    auto& noiseTex = m_resources.textures.get("assets/golf/images/wind.png");
    noiseTex.setRepeated(true);
    noiseTex.setSmooth(true);
    m_resources.materials.get(m_materialIDs[MaterialID::Billboard]).setProperty("u_noiseTexture", noiseTex);


    m_resources.shaders.loadFromString(ShaderID::Wireframe, WireframeVertex, WireframeFragment);
    m_materialIDs[MaterialID::Wireframe] = m_resources.materials.add(m_resources.shaders.get(ShaderID::Wireframe));
    m_resources.materials.get(m_materialIDs[MaterialID::Wireframe]).blendMode = cro::Material::BlendMode::Alpha;

    m_resources.shaders.loadFromString(ShaderID::WireframeCulled, WireframeVertex, WireframeFragment, "#define CULLED\n");
    m_materialIDs[MaterialID::WireframeCulled] = m_resources.materials.add(m_resources.shaders.get(ShaderID::WireframeCulled));
    m_resources.materials.get(m_materialIDs[MaterialID::WireframeCulled]).blendMode = cro::Material::BlendMode::Alpha;

    m_resources.shaders.loadFromString(ShaderID::Beacon, BeaconVertex, BeaconFragment, "#define TEXTURED\n");
    m_materialIDs[MaterialID::Beacon] = m_resources.materials.add(m_resources.shaders.get(ShaderID::Beacon));

    m_resources.shaders.loadFromString(ShaderID::Horizon, HorizonVert, HorizonFrag);
    m_materialIDs[MaterialID::Horizon] = m_resources.materials.add(m_resources.shaders.get(ShaderID::Horizon));

    //load the billboard rects from a sprite sheet and convert to templates
    cro::SpriteSheet spriteSheet;
    if (m_sharedData.treeQuality == SharedStateData::Classic)
    {
        spriteSheet.loadFromFile("assets/golf/sprites/shrubbery_low.spt", m_resources.textures);
    }
    else
    {
        spriteSheet.loadFromFile("assets/golf/sprites/shrubbery.spt", m_resources.textures);
    }
    m_billboardTemplates[BillboardID::Grass01] = spriteToBillboard(spriteSheet.getSprite("grass01"));
    m_billboardTemplates[BillboardID::Grass02] = spriteToBillboard(spriteSheet.getSprite("grass02"));
    m_billboardTemplates[BillboardID::Flowers01] = spriteToBillboard(spriteSheet.getSprite("flowers01"));
    m_billboardTemplates[BillboardID::Flowers02] = spriteToBillboard(spriteSheet.getSprite("flowers02"));
    m_billboardTemplates[BillboardID::Flowers03] = spriteToBillboard(spriteSheet.getSprite("flowers03"));
    m_billboardTemplates[BillboardID::Bush01] = spriteToBillboard(spriteSheet.getSprite("hedge01"));
    m_billboardTemplates[BillboardID::Bush02] = spriteToBillboard(spriteSheet.getSprite("hedge02"));

    m_billboardTemplates[BillboardID::Tree01] = spriteToBillboard(spriteSheet.getSprite("tree01"));
    m_billboardTemplates[BillboardID::Tree02] = spriteToBillboard(spriteSheet.getSprite("tree02"));
    m_billboardTemplates[BillboardID::Tree03] = spriteToBillboard(spriteSheet.getSprite("tree03"));
    m_billboardTemplates[BillboardID::Tree04] = spriteToBillboard(spriteSheet.getSprite("tree04"));

    //UI stuff
    spriteSheet.loadFromFile("assets/golf/sprites/ui.spt", m_resources.textures);
    m_sprites[SpriteID::PowerBar] = spriteSheet.getSprite("power_bar");
    m_sprites[SpriteID::PowerBarInner] = spriteSheet.getSprite("power_bar_inner");
    m_sprites[SpriteID::HookBar] = spriteSheet.getSprite("hook_bar");
    m_sprites[SpriteID::WindIndicator] = spriteSheet.getSprite("wind_dir");
    m_sprites[SpriteID::WindSpeed] = spriteSheet.getSprite("wind_speed");
    m_sprites[SpriteID::MessageBoard] = spriteSheet.getSprite("message_board");

    auto flagSprite = spriteSheet.getSprite("flag03");
    m_flagQuad.setTexture(*flagSprite.getTexture());
    m_flagQuad.setTextureRect(flagSprite.getTextureRect());

    //ball models - the menu should never have let us get this far if it found no ball files
    for (const auto& [colour, uid, path] : m_sharedData.ballModels)
    {
        std::unique_ptr<cro::ModelDefinition> def = std::make_unique<cro::ModelDefinition>(m_resources);
        if (def->loadFromFile(path))
        {
            m_ballModels.insert(std::make_pair(uid, std::move(def)));
        }
    }

    //club models
    cro::ModelDefinition md(m_resources);
    m_clubModels[ClubModel::Wood] = m_gameScene.createEntity();
    m_clubModels[ClubModel::Wood].addComponent<cro::Transform>();
    if (md.loadFromFile("assets/golf/models/club_wood.cmt"))
    {
        md.createModel(m_clubModels[ClubModel::Wood]);

        auto material = m_resources.materials.get(m_materialIDs[MaterialID::Cel]);
        applyMaterialData(md, material, 0);
        m_clubModels[ClubModel::Wood].getComponent<cro::Model>().setMaterial(0, material);
    }
    else
    {
        createFallbackModel(m_clubModels[ClubModel::Wood], m_resources);
    }


    m_clubModels[ClubModel::Iron] = m_gameScene.createEntity();
    m_clubModels[ClubModel::Iron].addComponent<cro::Transform>();
    if (md.loadFromFile("assets/golf/models/club_iron.cmt"))
    {
        md.createModel(m_clubModels[ClubModel::Iron]);

        auto material = m_resources.materials.get(m_materialIDs[MaterialID::Cel]);
        applyMaterialData(md, material, 0);
        m_clubModels[ClubModel::Iron].getComponent<cro::Model>().setMaterial(0, material);
    }
    else
    {
        createFallbackModel(m_clubModels[ClubModel::Iron], m_resources);
        m_clubModels[ClubModel::Iron].getComponent<cro::Model>().setMaterialProperty(0, "u_colour", cro::Colour::Cyan);
    }

    initAudio();
}

void DrivingState::initAudio()
{
    //8 evenly spaced points with ambient audio
    cro::AudioScape as;
    if (as.loadFromFile("assets/golf/sound/ambience.xas", m_resources.audio))
    {
        std::array emitterNames =
        {
            std::string("01"),
            std::string("02"),
            std::string("03"),
            std::string("04"),
            std::string("05"),
            std::string("06"),
            std::string("05"),
            std::string("06"),
        };
        std::shuffle(emitterNames.begin(), emitterNames.end(), cro::Util::Random::rndEngine);

        static constexpr float xOffset = RangeSize.x / 4.f;
        static constexpr float height = 4.f;
        static constexpr float zOffset = RangeSize.y / 4.f;
        static constexpr std::array positions =
        {
            glm::vec3(-xOffset, height, -zOffset * 2.f),
            glm::vec3(xOffset, height, -zOffset * 2.f),
            glm::vec3(-xOffset, height, -zOffset),
            glm::vec3(xOffset, height, -zOffset),
            glm::vec3(-xOffset, height, zOffset),
            glm::vec3(xOffset, height, zOffset),
            glm::vec3(-xOffset, height, zOffset * 2.f),
            glm::vec3(xOffset, height, zOffset * 2.f),
        };

        for (auto i = 0u; i < emitterNames.size(); ++i)
        {
            if (as.hasEmitter(emitterNames[i]))
            {
                auto entity = m_gameScene.createEntity();
                entity.addComponent<cro::Transform>().setPosition(positions[i]);
                entity.addComponent<cro::AudioEmitter>() = as.getEmitter(emitterNames[i]);
                entity.getComponent<cro::AudioEmitter>().play();
            }
        }

        //random incidental audio
        if (as.hasEmitter("incidental01")
            && as.hasEmitter("incidental02")
            && as.hasEmitter("church"))
        {
            auto entity = m_gameScene.createEntity();
            entity.addComponent<cro::AudioEmitter>() = as.getEmitter("incidental01");
            entity.getComponent<cro::AudioEmitter>().setLooped(false);
            auto plane01 = entity;

            entity = m_gameScene.createEntity();
            entity.addComponent<cro::AudioEmitter>() = as.getEmitter("incidental02");
            entity.getComponent<cro::AudioEmitter>().setLooped(false);
            auto plane02 = entity;

            entity = m_gameScene.createEntity();
            entity.addComponent<cro::AudioEmitter>() = as.getEmitter("church");
            entity.getComponent<cro::AudioEmitter>().setLooped(false);
            auto church = entity;

            cro::ModelDefinition md(m_resources);
            cro::Entity planeEnt;
            if (md.loadFromFile("assets/golf/models/plane.cmt"))
            {
                static constexpr glm::vec3 Start(-132.f, PlaneHeight, 20.f);
                static constexpr glm::vec3 End(252.f, PlaneHeight, -220.f);

                entity = m_gameScene.createEntity();
                entity.addComponent<cro::Transform>().setPosition(Start);
                entity.getComponent<cro::Transform>().rotate(cro::Transform::Y_AXIS, 32.f * cro::Util::Const::degToRad);
                entity.getComponent<cro::Transform>().setScale({ 0.01f, 0.01f, 0.01f });
                md.createModel(entity);

                entity.addComponent<cro::Callback>().function =
                    [](cro::Entity e, float dt)
                {
                    static constexpr float Speed = 6.3f;
                    const float MaxLen = glm::length2((Start - End) / 2.f);

                    auto& tx = e.getComponent<cro::Transform>();
                    auto dir = glm::normalize(tx.getRightVector()); //scaling means this isn't normalised :/
                    tx.move(dir * Speed * dt);

                    float currLen = glm::length2((Start + ((Start + End) / 2.f)) - tx.getPosition());
                    float scale = std::max(1.f - (currLen / MaxLen), 0.001f); //can't scale to 0 because it breaks normalizing the right vector above
                    tx.setScale({ scale, scale, scale });

                    if (tx.getPosition().x > End.x)
                    {
                        tx.setPosition(Start);
                        e.getComponent<cro::Callback>().active = false;
                    }
                };

                auto material = m_resources.materials.get(m_materialIDs[MaterialID::CelTextured]);
                applyMaterialData(md, material);
                entity.getComponent<cro::Model>().setMaterial(0, material);
                
                //engine
                entity.addComponent<cro::AudioEmitter>(); //always needs one in case audio doesn't exist
                if (as.hasEmitter("plane"))
                {
                    entity.getComponent<cro::AudioEmitter>() = as.getEmitter("plane");
                    entity.getComponent<cro::AudioEmitter>().setLooped(false);
                }

                planeEnt = entity;
            }

            struct AudioData final
            {
                float currentTime = 0.f;
                float timeout = static_cast<float>(cro::Util::Random::value(32, 64));
                cro::Entity activeEnt;
            };

            entity = m_gameScene.createEntity();
            entity.addComponent<cro::Callback>().active = true;
            entity.getComponent<cro::Callback>().setUserData<AudioData>();
            entity.getComponent<cro::Callback>().function =
                [plane01, plane02, church, planeEnt](cro::Entity e, float dt) mutable
            {
                auto& [currTime, timeOut, activeEnt] = e.getComponent<cro::Callback>().getUserData<AudioData>();

                if (!activeEnt.isValid()
                    || activeEnt.getComponent<cro::AudioEmitter>().getState() == cro::AudioEmitter::State::Stopped)
                {
                    currTime += dt;

                    if (currTime > timeOut)
                    {
                        currTime = 0.f;
                        timeOut = static_cast<float>(cro::Util::Random::value(120, 240));

                        auto id = cro::Util::Random::value(0, 3);
                        if (id == 0)
                        {
                            //fly the plane
                            if (planeEnt.isValid())
                            {
                                planeEnt.getComponent<cro::Callback>().active = true;
                                planeEnt.getComponent<cro::AudioEmitter>().play();
                                activeEnt = planeEnt;
                            }
                        }
                        else if (id == 1)
                        {
                            if (church.getComponent<cro::AudioEmitter>().getState() == cro::AudioEmitter::State::Stopped)
                            {
                                church.getComponent<cro::AudioEmitter>().play();
                                activeEnt = church;
                            }
                        }
                        else
                        {
                            auto ent = (id == 2) ? plane01 : plane02;
                            if (ent.getComponent<cro::AudioEmitter>().getState() == cro::AudioEmitter::State::Stopped)
                            {
                                ent.getComponent<cro::AudioEmitter>().play();
                                activeEnt = ent;
                            }
                        }
                    }
                }
            };

        }

        //put the new hole music on the cam for accessibilty
        //this is done *before* m_cameras is updated 
        if (as.hasEmitter("music"))
        {
            m_gameScene.getActiveCamera().addComponent<cro::AudioEmitter>() = as.getEmitter("music");
            m_gameScene.getActiveCamera().getComponent<cro::AudioEmitter>().setLooped(false);
        }
    }
    else
    {
        m_gameScene.getActiveCamera().addComponent<cro::AudioEmitter>();
        LogE << "Invalid AudioScape file was found" << std::endl;
    }

    //fades in the audio
    auto entity = m_gameScene.createEntity();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& progress = e.getComponent<cro::Callback>().getUserData<float>();
        progress = std::min(1.f, progress + dt);

        cro::AudioMixer::setPrefadeVolume(cro::Util::Easing::easeOutQuad(progress), MixerChannel::Effects);

        if (progress == 1)
        {
            e.getComponent<cro::Callback>().active = false;
            m_gameScene.destroyEntity(e);
        }
    };
}

void DrivingState::createScene()
{
    cro::AudioScape as;
    as.loadFromFile("assets/golf/sound/menu.xas", m_resources.audio);

    const auto& quitFail = [&](const std::string& msg)
    {
        //create a basic render texture in case a load
        //error occurs (this will be resized by camera callback on success)
        m_backgroundTexture.create(800, 600);

        m_sharedData.errorMessage = msg;
        requestStackPush(StateID::Error);
    };

    //check data file - quit if missing or corrupt
    cro::ConfigFile cfg;
    if (!cfg.loadFromFile("assets/golf/courses/driving.range"))
    {
        quitFail("Could Not Open Course Data");
        return;
    }

    const auto& properties = cfg.getProperties();
    for (const auto& p : properties)
    {
        const auto& name = p.getName();
        if (name == "hole")
        {
            auto& data = m_holeData.emplace_back();
            data.pin = p.getValue<glm::vec3>();
            data.target = data.pin;
            data.tee = PlayerPosition;
            //TODO this should be parsed from the cmt file
            data.modelPath = "assets/golf/models/driving_range.cmb"; //needed for ball system to load collision mesh
            //TODO check ball system for which properties are needed
        }
    }

    if (m_holeData.empty())
    {
        quitFail("No Hole Data Found");
        return;
    }

    const auto& objects = cfg.getObjects();
    for (const auto& obj : objects)
    {
        const auto& name = obj.getName();
        if (name == "prop")
        {
            std::string path;
            glm::vec3 position(0.f);
            float rotation = 0.f;

            std::vector<glm::vec3> targets;

            const auto& props = obj.getProperties();
            for (const auto& p : props)
            {
                const auto& propName = p.getName();
                if (propName == "model")
                {
                    path = p.getValue<std::string>();
                }
                else if (propName == "position")
                {
                    position = p.getValue<glm::vec3>();
                }
                else if (propName == "rotation")
                {
                    rotation = p.getValue<float>();
                }
            }

            //check for movement paths
            const auto propObjs = obj.getObjects();
            for (const auto& pObj : propObjs)
            {
                if (pObj.getName() == "path")
                {
                    const auto& points = pObj.getProperties();
                    for (const auto& point : points)
                    {
                        if (point.getName() == "point")
                        {
                            targets.push_back(point.getValue<glm::vec3>());
                        }
                    }
                }
            }

            cro::ModelDefinition md(m_resources);
            if (!path.empty()
                && md.loadFromFile(path))
            {
                auto entity = m_gameScene.createEntity();
                entity.addComponent<cro::Transform>().setPosition(position);
                entity.getComponent<cro::Transform>().setRotation(cro::Transform::Y_AXIS, rotation * cro::Util::Const::degToRad);
                md.createModel(entity);

                //not sure we need to set all submeshes - for one thing it breaks the cart shadow
                auto material = m_resources.materials.get(m_materialIDs[MaterialID::CelTextured]);
                applyMaterialData(md, material);
                entity.getComponent<cro::Model>().setMaterial(0, material);
                entity.getComponent<cro::Model>().setRenderFlags(~RenderFlags::MiniMap);

                //if we have a path to follow, add it
                if (!targets.empty())
                {
                    entity.addComponent<cro::Callback>().active = true;
                    entity.getComponent<cro::Callback>().setUserData<std::pair<std::size_t, std::vector<glm::vec3>>>(0, targets);
                    entity.getComponent<cro::Callback>().function =
                        [](cro::Entity e, float dt)
                    {
                        auto& [index, targets] = e.getComponent<cro::Callback>().getUserData<std::pair<std::size_t, std::vector<glm::vec3>>>();
                        auto& tx = e.getComponent<cro::Transform>();
                        auto dir = targets[index] - tx.getPosition();

                        float dist = glm::length(dir);
                        dir /= dist;

                        static constexpr float MinDist = 5.f;
                        static constexpr float MinSpeed = 7.f;
                        static constexpr float MaxSpeed = 14.f;

                        float multiplier = std::min(1.f, dist / MinDist);
                        float speed = MinSpeed + ((MaxSpeed - MinSpeed) * multiplier);

                        dir *= speed;
                        tx.move(dir * dt);

                        e.getComponent<cro::AudioEmitter>().setVelocity(dir);
                        e.getComponent<cro::AudioEmitter>().setPitch(speed / MaxSpeed);

                        if (dist < 0.2f)
                        {
                            index++;
                            if (index == targets.size())
                            {
                                e.getComponent<cro::AudioEmitter>().stop();
                                e.getComponent<cro::Callback>().active = false;
                            }
                        }
                    };

                    //this assumes we're a cart based on the fact we have target points, but hey
                    entity.addComponent<cro::AudioEmitter>() = as.getEmitter("cart");
                    entity.getComponent<cro::AudioEmitter>().play();

                    if (md.loadFromFile("assets/golf/models/menu/driver01.cmt"))
                    {
                        auto driver = m_gameScene.createEntity();
                        driver.addComponent<cro::Transform>();
                        md.createModel(driver);
                        entity.getComponent<cro::Transform>().addChild(driver.getComponent<cro::Transform>());
                    }
                }
            }
        }
    }

    //load the course model
    cro::ModelDefinition md(m_resources);
    if (!md.loadFromFile("assets/golf/models/driving_range.cmt"))
    {
        quitFail("Could Not Load Course Model");
        return;
    }

    //set the hole data for the first hole, just so the
    //ball system loads the collision mesh now
    if (!m_gameScene.getSystem<BallSystem>()->setHoleData(m_holeData[0]))
    {
        quitFail("Could not load collision data");
        return;
    }

    //we use this model twice, with a second copy for the minimap
    //it's a pita but the shadow receiving version has artifacts
    //when drawn in the mini view
    auto entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>();
    md.createModel(entity);

    auto count = entity.getComponent<cro::Model>().getMeshData().submeshCount;
    for (auto i = 0u; i < count; ++i)
    {
        auto material = m_resources.materials.get(m_materialIDs[MaterialID::Course]);
        applyMaterialData(md, material, i);
        entity.getComponent<cro::Model>().setMaterial(i, material);
    }
    entity.getComponent<cro::Model>().setRenderFlags(~RenderFlags::MiniMap);

    entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>();
    md.createModel(entity);

    for (auto i = 0u; i < count; ++i)
    {
        auto material = m_resources.materials.get(m_materialIDs[MaterialID::CelTextured]);
        applyMaterialData(md, material, i);
        entity.getComponent<cro::Model>().setMaterial(i, material);
    }
    entity.getComponent<cro::Model>().setRenderFlags(RenderFlags::MiniMap);


    //create the billboards
    createFoliage(entity);

    //and sky detail
    std::string skybox = "assets/golf/skyboxes/spring.sbf";
    //auto skyboxes = cro::FileSystem::listFiles(cro::FileSystem::getResourcePath() + skybox);
    //if (!skyboxes.empty())
    //{
    //    skybox += skyboxes[cro::Util::Random::value(0u, skyboxes.size() - 1)];
    //}
    auto cloudPath = loadSkybox(skybox, m_skyScene, m_resources, m_materialIDs[MaterialID::Horizon]);
    createClouds(cloudPath);

    //tee marker
    md.loadFromFile("assets/golf/models/tee_balls.cmt");
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(PlayerPosition);
    entity.getComponent<cro::Transform>().rotate(cro::Transform::Y_AXIS, 90.f * cro::Util::Const::degToRad);
    md.createModel(entity);
    entity.getComponent<cro::Model>().setMaterial(0, m_resources.materials.get(m_materialIDs[MaterialID::Cel]));

    createFlag();

    //update the 3D view
    auto updateView = [&](cro::Camera& cam)
    {
        auto vpSize = calcVPSize();

        auto winSize = glm::vec2(cro::App::getWindow().getSize());
        auto maxScale = std::floor(winSize.y / vpSize.y);
        float scale = m_sharedData.pixelScale ? maxScale : 1.f;
        auto texSize = winSize / scale;

        std::uint32_t samples = m_sharedData.pixelScale ? 0 :
            m_sharedData.antialias ? m_sharedData.multisamples : 0;

        m_sharedData.antialias =
            m_backgroundTexture.create(static_cast<std::uint32_t>(texSize.x), static_cast<std::uint32_t>(texSize.y), true, false, samples)
            && m_sharedData.multisamples != 0
            && !m_sharedData.pixelScale;

        auto invScale = (maxScale + 1.f) - scale;
        glCheck(glPointSize(invScale * BallPointSize));
        glCheck(glLineWidth(invScale));

        m_scaleBuffer.setData(invScale);

        ResolutionData d;
        d.resolution = texSize / invScale;
        m_resolutionBuffer.setData(d);

        cam.setPerspective(m_sharedData.fov * cro::Util::Const::degToRad, texSize.x / texSize.y, 0.1f, 320.f, 3);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };
    };

    static constexpr std::uint32_t ShadowMapSize = 2048u;
    auto camEnt = m_gameScene.getActiveCamera();
    auto& cam = camEnt.getComponent<cro::Camera>();
    cam.shadowMapBuffer.create(ShadowMapSize, ShadowMapSize);
    cam.resizeCallback = updateView;
    cam.setMaxShadowDistance(40.f);
    cam.setShadowExpansion(15.f);
    cam.renderFlags = ~RenderFlags::MiniMap;
    updateView(cam);
    
    m_cameras[CameraID::Player] = camEnt;

    static constexpr auto halfSize = RangeSize / 2.f;

    struct TransitionPath final
    {
        cro::Util::Maths::Spline targetPath;
        cro::Util::Maths::Spline cameraPath;

        const float TotalTime = 10.f;
        float currentTime = 0.f;
    }path;

    auto targetStart = glm::vec3(0.f, 4.5f, -160.f);
    path.targetPath.addPoint(targetStart);
    path.targetPath.addPoint(glm::vec3(0, 4.5f, -100.f));
    path.targetPath.addPoint(glm::vec3(0, 12.5f, -halfSize.y));
    path.targetPath.addPoint(glm::vec3(0.f, 4.5f, -halfSize.y));

    auto eyeStart = glm::vec3(0.f, CameraPosition.y, -halfSize.y - 20.f);
    path.cameraPath.addPoint(eyeStart);
    path.cameraPath.addPoint(glm::vec3(0.f, 32.5f, -halfSize.y / 3.f));
    path.cameraPath.addPoint(glm::vec3(0.f, 12.5f, halfSize.y / 2.f));
    path.cameraPath.addPoint(CameraPosition);

    auto tx = glm::inverse(glm::lookAt(eyeStart, targetStart, cro::Transform::Y_AXIS));
    camEnt.getComponent<cro::Transform>().setLocalTransform(tx);

    camEnt.addComponent<cro::Callback>().setUserData<TransitionPath>(path);
    camEnt.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& data = e.getComponent<cro::Callback>().getUserData<TransitionPath>();
        float oldTime = data.currentTime;
        data.currentTime = std::min(data.TotalTime, data.currentTime + dt);

        if (oldTime < data.TotalTime / 2.f
            && data.currentTime > data.TotalTime / 2.f)
        {
            //play the music
            e.getComponent<cro::AudioEmitter>().play();
        }

        float progress = cro::Util::Easing::easeInOutQuad(data.currentTime / data.TotalTime);

        auto target = data.targetPath.getInterpolatedPoint(progress);
        auto eye = data.cameraPath.getInterpolatedPoint(progress);
        auto tx = glm::inverse(glm::lookAt(eye, target, cro::Transform::Y_AXIS));

        e.getComponent<cro::Transform>().setLocalTransform(tx);

        if (data.currentTime == data.TotalTime)
        {
            e.getComponent<cro::Callback>().active = false;

            //position player sprite
            cro::Command cmd;
            cmd.targetFlags = CommandID::PlayerAvatar;
            cmd.action = [&](cro::Entity e, float)
            {
                e.getComponent<cro::Callback>().active = true;
            };
            m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

            //show menu
            cmd.targetFlags = CommandID::UI::DrivingBoard;
            m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

            //background fade
            m_summaryScreen.fadeEnt.getComponent<cro::Callback>().setUserData<float>(BackgroundAlpha);
            m_summaryScreen.fadeEnt.getComponent<cro::Callback>().active = true;
            m_summaryScreen.fadeEnt.getComponent<cro::Transform>().setPosition({ 0.f, 0.f, FadeDepth });
        }
    };



    //create an overhead camera
    auto setPerspective = [&](cro::Camera& cam)
    {
        auto vpSize = glm::vec2(cro::App::getWindow().getSize());

        cam.setPerspective(m_sharedData.fov * cro::Util::Const::degToRad, vpSize.x / vpSize.y, 0.1f, 320.f);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };
    };
    camEnt = m_gameScene.createEntity();
    camEnt.addComponent<cro::Transform>().setPosition({ RangeSize.x / 3.f, SkyCamHeight, 10.f });
    camEnt.addComponent<cro::Camera>().resizeCallback =
        [&, camEnt](cro::Camera& cam) //use explicit callback so we can capture the entity and use it to zoom via CamFollowSystem
    {
        auto vpSize = glm::vec2(cro::App::getWindow().getSize());
        cam.setPerspective((m_sharedData.fov* cro::Util::Const::degToRad) * camEnt.getComponent<CameraFollower>().zoom.fov, vpSize.x / vpSize.y, 0.1f, vpSize.x, 2);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };
    };
    camEnt.getComponent<cro::Camera>().setMaxShadowDistance(80.f);
    camEnt.getComponent<cro::Camera>().active = false;
    camEnt.getComponent<cro::Camera>().renderFlags = ~RenderFlags::MiniMap;
    camEnt.getComponent<cro::Camera>().shadowMapBuffer.create(ShadowMapSize, ShadowMapSize);
    camEnt.addComponent<cro::CommandTarget>().ID = CommandID::SpectatorCam;
    camEnt.addComponent<CameraFollower>().radius = 85.f * 85.f;
    camEnt.getComponent<CameraFollower>().id = CameraID::Sky;
    camEnt.getComponent<CameraFollower>().zoom.target = 0.1f;
    camEnt.getComponent<CameraFollower>().zoom.speed = 3.f;
    camEnt.addComponent<cro::AudioListener>();
    camEnt.addComponent<TargetInfo>(); //fudge because follower system requires it (water plane would be attached to this if it existed).
    setPerspective(camEnt.getComponent<cro::Camera>());
    m_cameras[CameraID::Sky] = camEnt;

    //and a green camera
    camEnt = m_gameScene.createEntity();
    camEnt.addComponent<cro::Transform>();
    camEnt.addComponent<cro::Camera>().resizeCallback =
        [&,camEnt](cro::Camera& cam)
    {
        auto vpSize = glm::vec2(cro::App::getWindow().getSize());
        cam.setPerspective((m_sharedData.fov* cro::Util::Const::degToRad) * camEnt.getComponent<CameraFollower>().zoom.fov, vpSize.x / vpSize.y, 0.1f, vpSize.x, 2);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };
    };
    camEnt.getComponent<cro::Camera>().active = false;
    camEnt.getComponent<cro::Camera>().renderFlags = ~RenderFlags::MiniMap;
    camEnt.getComponent<cro::Camera>().setMaxShadowDistance(50.f);
    camEnt.getComponent<cro::Camera>().shadowMapBuffer.create(ShadowMapSize, ShadowMapSize);
    camEnt.addComponent<cro::CommandTarget>().ID = CommandID::SpectatorCam;
    camEnt.addComponent<CameraFollower>().radius = 20.f * 20.f;
    camEnt.getComponent<CameraFollower>().id = CameraID::Green;
    camEnt.getComponent<CameraFollower>().zoom.speed = 2.f;
    camEnt.addComponent<cro::AudioListener>();
    camEnt.addComponent<TargetInfo>();
    setPerspective(camEnt.getComponent<cro::Camera>());
    m_cameras[CameraID::Green] = camEnt;


#ifdef CRO_DEBUG_
    camEnt = m_gameScene.createEntity();
    camEnt.addComponent<cro::Transform>();
    camEnt.addComponent<cro::Camera>().resizeCallback = updateView;
    camEnt.getComponent<cro::Camera>().shadowMapBuffer.create(ShadowMapSize, ShadowMapSize);
    //camEnt.getComponent<cro::Camera>().reflectionBuffer.create(1024, 1024);
    camEnt.addComponent<cro::AudioListener>();
    camEnt.addComponent<FpsCamera>();
    updateView(camEnt.getComponent<cro::Camera>());

    m_freeCam = camEnt;
#endif


    //emulate facing north with sun more or less behind player
    auto sunEnt = m_gameScene.getSunlight();
    sunEnt.getComponent<cro::Transform>().rotate(cro::Transform::X_AXIS, -65.f * cro::Util::Const::degToRad);
    sunEnt.getComponent<cro::Transform>().rotate(cro::Transform::Y_AXIS, -15.f * cro::Util::Const::degToRad);


    //we only want these to happen if the scene creation was successful
    createUI();
    startTransition();
}

void DrivingState::createFoliage(cro::Entity terrainEnt)
{
    //render a heightmap from the hole mesh
    //TODO this is lifted from TerrainBuilder and can probably be shared between both with a refactor
    const auto& meshData = terrainEnt.getComponent<cro::Model>().getMeshData();
    std::size_t normalOffset = 0;
    for (auto i = 0u; i < cro::Mesh::Normal; ++i)
    {
        normalOffset += meshData.attributes[i];
    }

    cro::Shader normalShader;
    normalShader.loadFromString(NormalMapVertexShader, NormalMapFragmentShader);

    glm::mat4 viewMat = glm::rotate(glm::mat4(1.f), cro::Util::Const::PI / 2.f, glm::vec3(1.f, 0.f, 0.f));
    glm::vec2 mapSize(280.f, 290.f);
    glm::mat4 projMat = glm::ortho(-mapSize.x / 2.f, mapSize.x / 2.f, -125.f, mapSize.y - 125.f, -10.f, 20.f);
    auto normalViewProj = projMat * viewMat;

    const auto& attribs = normalShader.getAttribMap();
    auto vaoCount = static_cast<std::int32_t>(meshData.submeshCount);

    std::vector<std::uint32_t> vaos(vaoCount);
    glCheck(glGenVertexArrays(vaoCount, vaos.data()));

    for (auto i = 0u; i < vaos.size(); ++i)
    {
        glCheck(glBindVertexArray(vaos[i]));
        glCheck(glBindBuffer(GL_ARRAY_BUFFER, meshData.vbo));
        glCheck(glEnableVertexAttribArray(attribs[cro::Mesh::Position]));
        glCheck(glVertexAttribPointer(attribs[cro::Mesh::Position], 3, GL_FLOAT, GL_FALSE, static_cast<std::int32_t>(meshData.vertexSize), 0));
        glCheck(glEnableVertexAttribArray(attribs[cro::Mesh::Normal]));
        glCheck(glVertexAttribPointer(attribs[cro::Mesh::Normal], 3, GL_FLOAT, GL_FALSE, static_cast<std::int32_t>(meshData.vertexSize), (void*)(normalOffset * sizeof(float))));
        glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshData.indexData[i].ibo));
    }

    glCheck(glUseProgram(normalShader.getGLHandle()));
    glCheck(glDisable(GL_CULL_FACE));

    float holeBottom = std::min(meshData.boundingBox[0].y, meshData.boundingBox[1].y);
    float holeHeight = std::max(meshData.boundingBox[0].y, meshData.boundingBox[1].y) - holeBottom;
    glCheck(glUniform1f(normalShader.getUniformID("u_lowestPoint"), holeBottom));
    glCheck(glUniform1f(normalShader.getUniformID("u_maxHeight"), holeHeight));
    glCheck(glUniformMatrix4fv(normalShader.getUniformID("u_projectionMatrix"), 1, GL_FALSE, &normalViewProj[0][0]));

    cro::RenderTexture normalMap;
    normalMap.create(280, 290, false); //course size + borders

    //clear the alpha to 0 so unrendered areas have zero height
    static const cro::Colour ClearColour = cro::Colour(0x7f7fff00);
    normalMap.clear(ClearColour);
    for (auto i = 0u; i < vaos.size(); ++i)
    {
        glCheck(glBindVertexArray(vaos[i]));
        glCheck(glDrawElements(GL_TRIANGLES, meshData.indexData[i].indexCount, GL_UNSIGNED_INT, 0));
    }
    normalMap.display();

    glCheck(glBindVertexArray(0));
    glCheck(glDeleteVertexArrays(vaoCount, vaos.data()));

    cro::Image normalMapImage;
    normalMap.getTexture().saveToImage(normalMapImage);

#ifdef CRO_DEBUG_
    m_debugHeightmap.loadFromImage(normalMapImage);
#endif

    const auto readHeightMap = [&](std::uint32_t x, std::uint32_t y)
    {
        auto size = normalMapImage.getSize();
        x = std::min(size.x - 1, std::max(0u, x));
        y = std::min(size.y - 1, std::max(0u, y));

        float height = static_cast<float>(normalMapImage.getPixel(x, y)[3]) / 255.f;
        return holeBottom + (holeHeight * height);
    };

    auto createBillboards = [&](cro::Entity dst, std::array<float, 2u> minBounds, std::array<float, 2u> maxBounds, float radius = 0.f, glm::vec2 centre = glm::vec2(0.f))
    {
        auto trees = pd::PoissonDiskSampling(4.f, minBounds, maxBounds);
        auto flowers = pd::PoissonDiskSampling(2.f, minBounds, maxBounds);
        std::vector<cro::Billboard> billboards;

        glm::vec3 offsetPos = dst.getComponent<cro::Transform>().getPosition();
        static constexpr glm::vec2 centreOffset(140.f, 125.f);

        const float radSqr = radius * radius;

        for (auto [x, y] : trees)
        {
            glm::vec2 radPos(x, y);
            auto len2 = glm::length2(radPos - centre);

            if (len2 < radSqr) continue;

            glm::vec2 mapPos(offsetPos.x + x, -offsetPos.z + y);
            mapPos += centreOffset;

            float scale = static_cast<float>(cro::Util::Random::value(12, 22)) / 10.f;
            auto& bb = billboards.emplace_back(m_billboardTemplates[cro::Util::Random::value(BillboardID::Tree01, BillboardID::Tree04)]);
            bb.position = { x, readHeightMap(static_cast<std::int32_t>(mapPos.x), static_cast<std::int32_t>(mapPos.y)) - 0.05f, -y}; //small vertical offset to stop floating billboards
            bb.size *= scale;
        }

        for (auto [x, y] : flowers)
        {
            glm::vec2 radPos(x, y);
            auto len2 = glm::length2(radPos - centre);

            if (len2 < radSqr) continue;

            glm::vec2 mapPos(offsetPos.x + x, -offsetPos.z + y);
            mapPos += centreOffset;

            float scale = static_cast<float>(cro::Util::Random::value(13, 17)) / 10.f;

            auto& bb = billboards.emplace_back(m_billboardTemplates[cro::Util::Random::value(BillboardID::Flowers01, BillboardID::Bush02)]);
            bb.position = { x, readHeightMap(static_cast<std::int32_t>(mapPos.x), static_cast<std::int32_t>(mapPos.y)) + 0.05f, -y };
            bb.size *= scale;
        }
        dst.getComponent<cro::BillboardCollection>().setBillboards(billboards);
        dst.getComponent<cro::Model>().setRenderFlags(~(RenderFlags::MiniMap));
    };

    cro::ModelDefinition md(m_resources);
    const std::string shrubPath = m_sharedData.treeQuality == SharedStateData::Classic ?
        ("assets/golf/models/shrubbery_low.cmt") :
        ("assets/golf/models/shrubbery.cmt");

    //sides
    for (auto i = 0u; i < ChunkCount; ++i)
    {
        glm::vec3 pos = { (-RangeSize.x / 2.f) - BillboardChunk.x, -FoliageCallback::Distance, (i * -BillboardChunk.y) + (RangeSize.y / 2.f) };
        for (auto j = 0u; j < 2u; ++j)
        {
            md.loadFromFile(shrubPath);

            auto entity = m_gameScene.createEntity();
            entity.addComponent<cro::Transform>().setPosition(pos);
            entity.addComponent<cro::Callback>().active = true;
            entity.getComponent<cro::Callback>().function = FoliageCallback(static_cast<float>(ChunkCount - i));
            md.createModel(entity);

            if (entity.hasComponent<cro::BillboardCollection>())
            {
                static constexpr std::array MinBounds = { 0.f, 0.f };
                static constexpr std::array MaxBounds = { BillboardChunk.x, BillboardChunk.y };
                createBillboards(entity, MinBounds, MaxBounds);

                auto material = m_resources.materials.get(m_materialIDs[MaterialID::Billboard]);
                applyMaterialData(md, material);
                entity.getComponent<cro::Model>().setMaterial(0, material);
            }

            pos.x += RangeSize.x + BillboardChunk.x;
        }
    }

    //end range of trees
    md.loadFromFile(shrubPath);
    auto entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ (-RangeSize.x / 2.f) - BillboardChunk.x, -FoliageCallback::Distance, (-RangeSize.y / 2.f) });
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function = FoliageCallback();
    md.createModel(entity);
    if (entity.hasComponent<cro::BillboardCollection>())
    {
        createBillboards(entity, { 0.f, 0.f }, { RangeSize.x + (BillboardChunk.x * 2.f), BillboardChunk.x });

        auto material = m_resources.materials.get(m_materialIDs[MaterialID::Billboard]);
        applyMaterialData(md, material);
        entity.getComponent<cro::Model>().setMaterial(0, material);
    }

    //magic height number here - should match the loaded pavilion height
    glm::vec3 position((-RangeSize.x / 2.f) - BillboardChunk.x, 2.f, (-RangeSize.y / 2.f) - (BillboardChunk.x * 1.6f));
    for (auto i = 0; i < 2; ++i)
    {
        md.loadFromFile(shrubPath);
        entity = m_gameScene.createEntity();
        entity.addComponent<cro::Transform>().setPosition(position);
        md.createModel(entity);

        if (entity.hasComponent<cro::BillboardCollection>())
        {
            createBillboards(entity, { 0.f, 0.f }, { (BillboardChunk.x * 2.8f), BillboardChunk.x / 2.f });

            auto material = m_resources.materials.get(m_materialIDs[MaterialID::Billboard]);
            applyMaterialData(md, material);
            entity.getComponent<cro::Model>().setMaterial(0, material);
        }

        position.x += 170.f;
    }


    //curved copse behind the player
    md.loadFromFile(shrubPath);
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ (-RangeSize.x / 2.f), 0.f, (RangeSize.y / 2.f) + 20.f });
    md.createModel(entity);
    if (entity.hasComponent<cro::BillboardCollection>())
    {
        createBillboards(entity, { 0.f, 0.f }, { RangeSize.x , BillboardChunk.x }, BillboardChunk.x - 15.f, { RangeSize.x / 2.f, BillboardChunk.x });

        auto material = m_resources.materials.get(m_materialIDs[MaterialID::Billboard]);
        applyMaterialData(md, material);
        entity.getComponent<cro::Model>().setMaterial(0, material);
    }
}

void DrivingState::createClouds(const std::string& cloudPath)
{
    auto spritePath = cloudPath.empty() ? "assets/golf/sprites/clouds.spt" : cloudPath;

    cro::SpriteSheet spriteSheet;
    if (spriteSheet.loadFromFile(spritePath, m_resources.textures)
        && spriteSheet.getSprites().size() > 1)
    {
        const auto& sprites = spriteSheet.getSprites();
        std::vector<cro::Sprite> randSprites;
        for (auto [_, sprite] : sprites)
        {
            randSprites.push_back(sprite);
        }

        m_resources.shaders.loadFromString(ShaderID::Cloud, CloudVertex, CloudFragment);
        auto& shader = m_resources.shaders.get(ShaderID::Cloud);
        m_scaleBuffer.addShader(shader);

        auto matID = m_resources.materials.add(shader);
        auto material = m_resources.materials.get(matID);
        material.blendMode = cro::Material::BlendMode::Alpha;
        material.setProperty("u_texture", *spriteSheet.getTexture());

        auto seed = static_cast<std::uint32_t>(std::time(nullptr));
        static constexpr std::array MinBounds = { 0.f, 0.f };
        static constexpr std::array MaxBounds = { 320.f, 320.f };
        auto positions = pd::PoissonDiskSampling(150.f, MinBounds, MaxBounds, 30u, seed);

        auto Offset = 160.f;

        std::vector<cro::Entity> delayedUpdates;

        for (const auto& position : positions)
        {
            float height = cro::Util::Random::value(20, 40) + PlaneHeight;
            glm::vec3 cloudPos(position[0] - Offset, height, -position[1] + Offset);


            auto entity = m_gameScene.createEntity();
            entity.addComponent<cro::Transform>().setPosition(cloudPos);
            entity.addComponent<Cloud>().speedMultiplier = static_cast<float>(cro::Util::Random::value(10, 22)) / 100.f;
            entity.addComponent<cro::Sprite>() = randSprites[cro::Util::Random::value(0u, randSprites.size() - 1)];
            entity.addComponent<cro::Model>();

            auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
            bounds.width /= PixelPerMetre;
            bounds.height /= PixelPerMetre;
            entity.getComponent<cro::Transform>().setOrigin({bounds.width / 2.f, bounds.height / 2.f, 0.f});

            float scale = static_cast<float>(cro::Util::Random::value(4, 10));
            entity.getComponent<cro::Transform>().setScale(glm::vec3(scale));
            entity.getComponent<cro::Transform>().rotate(cro::Transform::X_AXIS, 90.f * cro::Util::Const::degToRad);
            //entity.getComponent<cro::Transform>().rotate(cro::Transform::Z_AXIS, 180.f * cro::Util::Const::degToRad);

            delayedUpdates.push_back(entity);
        }

        //this is a work around because changing sprite 3D materials
        //require at least once scene update to be run first.
        auto entity = m_uiScene.createEntity();
        entity.addComponent<cro::Callback>().active = true;
        entity.getComponent<cro::Callback>().function =
            [&, material, delayedUpdates](cro::Entity e, float)
        {
            for (auto en : delayedUpdates)
            {
                en.getComponent<cro::Model>().setMaterial(0, material);
            }

            e.getComponent<cro::Callback>().active = false;
            m_uiScene.destroyEntity(e);
        };
    }
}

void DrivingState::createPlayer(cro::Entity courseEnt)
{
    //load from avatar info
    const auto indexFromSkinID = [&](std::uint32_t skinID)->std::size_t
    {
        auto result = std::find_if(m_sharedData.avatarInfo.begin(), m_sharedData.avatarInfo.end(),
            [skinID](const SharedStateData::AvatarInfo& ai)
            {
                return skinID == ai.uid;
            });

        if (result != m_sharedData.avatarInfo.end())
        {
            return std::distance(m_sharedData.avatarInfo.begin(), result);
        }
        return 0;
    };

    auto playerIndex = cro::Util::Random::value(0, 3);
    const auto& playerData = m_sharedData.localConnectionData.playerData[playerIndex];
    auto idx = indexFromSkinID(playerData.skinID);

    //3D Player Model
    cro::ModelDefinition md(m_resources);
    md.loadFromFile(m_sharedData.avatarInfo[idx].modelPath);
    auto entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(PlayerPosition);
    entity.addComponent<cro::CommandTarget>().ID = CommandID::PlayerAvatar;
    md.createModel(entity);

    entity.getComponent<cro::Transform>().setScale(glm::vec3(1.f, 0.f, 0.f));
    entity.addComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function =
        [](cro::Entity e, float dt)
    {
        auto& scale = e.getComponent<cro::Callback>().getUserData<float>();
        scale = std::min(1.f, scale + (dt * 2.f));

        auto dir = e.getComponent<cro::Transform>().getScale().x; //might be flipped
        e.getComponent<cro::Transform>().setScale(glm::vec2(dir, cro::Util::Easing::easeOutBounce(scale)));

        if (scale == 1)
        {
            scale = 0.f;
            e.getComponent<cro::Callback>().active = false;
        }
    };


    if (playerData.flipped)
    {
        entity.getComponent<cro::Transform>().setScale({ -1.f, 0.f, 0.f });
        entity.getComponent<cro::Model>().setFacing(cro::Model::Facing::Back);
        playerXScale = -1.f; //used to flip the hook/slice message

        m_clubModels[ClubModel::Wood].getComponent<cro::Model>().setFacing(cro::Model::Facing::Back);
        m_clubModels[ClubModel::Iron].getComponent<cro::Model>().setFacing(cro::Model::Facing::Back);
    }

    //avatar requirement is single material
    auto material = m_resources.materials.get(m_materialIDs[MaterialID::CelTexturedSkinned]);
    applyMaterialData(md, material);
    material.setProperty("u_diffuseMap", m_sharedData.avatarTextures[0][playerIndex]);
    entity.getComponent<cro::Model>().setMaterial(0, material);

    if (entity.hasComponent<cro::Skeleton>())
    {
        //map the animation IDs
        auto& skel = entity.getComponent<cro::Skeleton>();
        const auto& anims = skel.getAnimations();
        for (auto i = 0u; i < anims.size(); ++i)
        {
            if (anims[i].name == "idle")
            {
                m_avatar.animationIDs[AnimationID::Idle] = i;
            }
            else if (anims[i].name == "drive")
            {
                m_avatar.animationIDs[AnimationID::Swing] = i;
            }
            else if (anims[i].name == "chip")
            {
                m_avatar.animationIDs[AnimationID::Chip] = i;
            }
        }

        //find attachment points for club model
        auto id = skel.getAttachmentIndex("hands");
        if (id > -1)
        {
            m_avatar.handsAttachment = &skel.getAttachments()[id];
            m_avatar.handsAttachment->setModel(m_clubModels[ClubModel::Wood]);
        }
        else
        {
            //although this should have been validated when loading
            //avatar data in to the menu
            LogW << "No attachment point named \'hands\' was found" << std::endl;
        }

        id = skel.getAttachmentIndex("head");
        if (id > -1)
        {
            //see if we have a hair model
            std::int32_t hairID = 0;
            if (auto hair = std::find_if(m_sharedData.hairInfo.begin(), m_sharedData.hairInfo.end(),
                [&](const SharedStateData::HairInfo& h) {return h.uid == playerData.hairID; });
                hair != m_sharedData.hairInfo.end())
            {
                hairID = static_cast<std::int32_t>(std::distance(m_sharedData.hairInfo.begin(), hair));
            }

            if (hairID != 0
                && md.loadFromFile(m_sharedData.hairInfo[hairID].modelPath))
            {
                auto hairEnt = m_gameScene.createEntity();
                hairEnt.addComponent<cro::Transform>();
                md.createModel(hairEnt);

                //set material and colour
                auto mat = m_resources.materials.get(m_materialIDs[MaterialID::Hair]);
                mat.setProperty("u_hairColour", cro::Colour(pc::Palette[playerData.avatarFlags[pc::ColourKey::Hair]].light));
                hairEnt.getComponent<cro::Model>().setMaterial(0, mat);

                skel.getAttachments()[id].setModel(hairEnt);

                if (playerData.flipped)
                {
                    hairEnt.getComponent<cro::Model>().setFacing(cro::Model::Facing::Back);
                }

                //fade callback
                hairEnt.addComponent<cro::Callback>().active = true;
                hairEnt.getComponent<cro::Callback>().function =
                    [&](cro::Entity e, float)
                {
                    float alpha = std::abs(m_inputParser.getYaw());
                    alpha = cro::Util::Easing::easeOutQuart(1.f - (alpha / (m_inputParser.getMaxRotation() * 1.06f)));

                    e.getComponent<cro::Model>().setMaterialProperty(0, "u_fadeAmount", alpha);
                };
            }
        }

        //skel.setInterpolationEnabled(false);
    }

    auto playerEnt = entity;

    //displays the stroke direction
    auto pos = PlayerPosition;
    pos.y += 0.01f;
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(pos);
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, playerEnt](cro::Entity e, float) mutable
    {
        e.getComponent<cro::Transform>().setRotation(cro::Transform::Y_AXIS, m_inputParser.getYaw());
        
        //fade the player sprite at high angles
        //so we don't obstruct the view of the indicator

        //we have to do this here as the player ent has a different callback func.
        float alpha = std::abs(m_inputParser.getYaw());
        alpha = cro::Util::Easing::easeOutQuart(1.f - (alpha / (m_inputParser.getMaxRotation() * 1.06f)));

        playerEnt.getComponent<cro::Model>().setMaterialProperty(0, "u_fadeAmount", alpha);
    };
    //entity.addComponent<cro::CommandTarget>().ID = CommandID::StrokeIndicator;

    auto meshID = m_resources.meshes.loadMesh(cro::DynamicMeshBuilder(cro::VertexProperty::Position | cro::VertexProperty::Colour, 1, GL_LINE_STRIP));
    material = m_resources.materials.get(m_materialIDs[MaterialID::Wireframe]);
    material.blendMode = cro::Material::BlendMode::Additive;
    entity.addComponent<cro::Model>(m_resources.meshes.getMesh(meshID), material);
    auto* meshData = &entity.getComponent<cro::Model>().getMeshData();

    
    std::vector<float> verts =
    {
        0.f, Ball::Radius, 0.005f,  1.f * IndicatorLightness, 0.97f * IndicatorLightness, 0.88f * IndicatorLightness, 1.f,
        0.f, Ball::Radius, -5.f,    1.f * IndicatorDarkness, 0.97f * IndicatorDarkness, 0.88f * IndicatorDarkness, 0.2f,
        0.f, Ball::Radius, 0.005f,  1.f * IndicatorLightness, 0.97f * IndicatorLightness, 0.88f * IndicatorLightness, 1.f
    };
    std::vector<std::uint32_t> indices =
    {
        0,1,2
    };


    auto vertStride = (meshData->vertexSize / sizeof(float));
    meshData->vertexCount = verts.size() / vertStride;
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, meshData->vbo));
    glCheck(glBufferData(GL_ARRAY_BUFFER, meshData->vertexSize * meshData->vertexCount, verts.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, 0));

    auto* submesh = &meshData->indexData[0];
    submesh->indexCount = static_cast<std::uint32_t>(indices.size());
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, submesh->ibo));
    glCheck(glBufferData(GL_ELEMENT_ARRAY_BUFFER, submesh->indexCount * sizeof(std::uint32_t), indices.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    entity.getComponent<cro::Model>().setHidden(true);
    entity.getComponent<cro::Model>().setRenderFlags(~(RenderFlags::MiniGreen | RenderFlags::MiniMap));
    auto indicatorEnt = entity;

    //a 'fan' which shows max rotation
    meshID = m_resources.meshes.loadMesh(cro::DynamicMeshBuilder(cro::VertexProperty::Position | cro::VertexProperty::Colour, 1, GL_TRIANGLE_FAN));
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::StrokeArc;
    entity.addComponent<cro::Model>(m_resources.meshes.getMesh(meshID), material);
    entity.addComponent<cro::Transform>().setPosition(pos);
    entity.getComponent<cro::Transform>().rotate(cro::Transform::Y_AXIS, 90.f * cro::Util::Const::degToRad);
    entity.getComponent<cro::Transform>().setScale({ 1.f, 1.f, 0.f });
    entity.getComponent<cro::Model>().setHidden(true);
    entity.getComponent<cro::Model>().setRenderFlags(~(RenderFlags::MiniGreen | RenderFlags::MiniMap));

    struct FanData final
    {
        std::int32_t dir = 1;
        float progress = 0.f;
    };
    entity.addComponent<cro::Callback>().setUserData<FanData>();
    entity.getComponent<cro::Callback>().function =
        [indicatorEnt](cro::Entity e, float dt) mutable
    {
        const float Speed = dt * 3.f;

        auto& [dir, progress] = e.getComponent<cro::Callback>().getUserData<FanData>();
        if (dir == 1)
        {
            //grow
            progress = std::min(1.f, progress + Speed);
            e.getComponent<cro::Model>().setHidden(false);
            indicatorEnt.getComponent<cro::Model>().setHidden(false);

            if (progress == 1)
            {
                dir = 0;
                e.getComponent<cro::Callback>().active = false;
            }
        }
        else
        {
            progress = std::max(0.f, progress - Speed);

            if (progress == 0)
            {
                dir = 1;
                e.getComponent<cro::Callback>().active = false;

                e.getComponent<cro::Model>().setHidden(true);
                indicatorEnt.getComponent<cro::Model>().setHidden(true);
            }
        }
        auto scale = e.getComponent<cro::Transform>().getScale();
        scale.z = cro::Util::Easing::easeOutQuad(progress);
        e.getComponent<cro::Transform>().setScale(scale);
        indicatorEnt.getComponent<cro::Transform>().setScale(scale);
    };

    const float pointCount = 5.f;
    const float arc = m_inputParser.getMaxRotation() * 2.f;
    const float step = arc / pointCount;
    const float radius = 2.5f;

    std::vector<glm::vec2> points;
    for (auto i = -m_inputParser.getMaxRotation(); i <= -m_inputParser.getMaxRotation() + arc; i += step)
    {
        auto& p = points.emplace_back(std::cos(i), std::sin(i));
        p *= radius;
    }

    glm::vec3 c = { TextGoldColour.getRed(), TextGoldColour.getGreen(), TextGoldColour.getBlue() };
    c *= IndicatorLightness / 10.f;
    meshData = &entity.getComponent<cro::Model>().getMeshData();
    verts =
    {
        0.f, Ball::Radius, 0.f,                      c.r, c.g, c.b, 1.f,
        points[0].x, Ball::Radius, -points[0].y,     c.r, c.g, c.b, 1.f,
        points[1].x, Ball::Radius, -points[1].y,     c.r, c.g, c.b, 1.f,
        points[2].x, Ball::Radius, -points[2].y,     c.r, c.g, c.b, 1.f,
        points[3].x, Ball::Radius, -points[3].y,     c.r, c.g, c.b, 1.f,
        points[4].x, Ball::Radius, -points[4].y,     c.r, c.g, c.b, 1.f,
        points[5].x, Ball::Radius, -points[5].y,     c.r, c.g, c.b, 1.f
    };
    indices =
    {
        0,1,2,3,4,5,6
    };
    meshData->vertexCount = verts.size() / vertStride;
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, meshData->vbo));
    glCheck(glBufferData(GL_ARRAY_BUFFER, meshData->vertexSize * meshData->vertexCount, verts.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, 0));

    submesh = &meshData->indexData[0];
    submesh->indexCount = static_cast<std::uint32_t>(indices.size());
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, submesh->ibo));
    glCheck(glBufferData(GL_ELEMENT_ARRAY_BUFFER, submesh->indexCount * sizeof(std::uint32_t), indices.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
}

void DrivingState::createBall()
{
    //ball is rendered as a single point
    //at a distance, and as a model when closer
    //glCheck(glPointSize(BallPointSize)); - this is set in resize callback based on the buffer resolution/pixel scale

    auto ballMaterialID = m_materialIDs[MaterialID::WireframeCulled];
    auto ballMeshID = m_resources.meshes.loadMesh(cro::DynamicMeshBuilder(cro::VertexProperty::Position | cro::VertexProperty::Colour, 1, GL_POINTS));
    auto shadowMeshID = m_resources.meshes.loadMesh(cro::DynamicMeshBuilder(cro::VertexProperty::Position | cro::VertexProperty::Colour, 1, GL_POINTS));

    auto* meshData = &m_resources.meshes.getMesh(ballMeshID);
    std::vector<float> verts =
    {
        0.f, 0.f, 0.f,   1.f, 1.f, 1.f, 1.f
    };
    std::vector<std::uint32_t> indices =
    {
        0
    };

    meshData->vertexCount = 1;
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, meshData->vbo));
    glCheck(glBufferData(GL_ARRAY_BUFFER, meshData->vertexSize * meshData->vertexCount, verts.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, 0));

    auto* submesh = &meshData->indexData[0];
    submesh->indexCount = 1;
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, submesh->ibo));
    glCheck(glBufferData(GL_ELEMENT_ARRAY_BUFFER, submesh->indexCount * sizeof(std::uint32_t), indices.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    meshData = &m_resources.meshes.getMesh(shadowMeshID);
    verts =
    {
        0.f, 0.f, 0.f,    0.5f, 0.5f, 0.5f, 1.f,
    };
    meshData->vertexCount = 1;
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, meshData->vbo));
    glCheck(glBufferData(GL_ARRAY_BUFFER, meshData->vertexSize * meshData->vertexCount, verts.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, 0));

    submesh = &meshData->indexData[0];
    submesh->indexCount = 1;
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, submesh->ibo));
    glCheck(glBufferData(GL_ELEMENT_ARRAY_BUFFER, submesh->indexCount * sizeof(std::uint32_t), indices.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));



    auto ballID = m_sharedData.localConnectionData.playerData[cro::Util::Random::value(0,3)].ballID;

    //render the ball as a point so no perspective is applied to the scale
    auto material = m_resources.materials.get(ballMaterialID);
    material.setProperty("u_colour", TextNormalColour);
    auto ball = std::find_if(m_sharedData.ballModels.begin(), m_sharedData.ballModels.end(),
        [ballID](const SharedStateData::BallInfo& ballPair)
        {
            return ballPair.uid == ballID;
        });
    if (ball != m_sharedData.ballModels.end())
    {
        material.setProperty("u_colour", ball->tint);
    }
    else
    {
        //this should at least line up with the fallback model
        material.setProperty("u_colour", m_sharedData.ballModels.begin()->tint);
    }

    auto entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(PlayerPosition);
    entity.getComponent<cro::Transform>().setOrigin({ 0.f, -0.003f, 0.f }); //pushes the ent above the ground a bit to stop Z fighting
    entity.addComponent<cro::Model>(m_resources.meshes.getMesh(ballMeshID), material);
    entity.getComponent<cro::Model>().setRenderFlags(~RenderFlags::MiniMap);
    entity.addComponent<Ball>();
    entity.addComponent<ClientCollider>(); //needed to fudge the operation of cam follower system
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Ball;
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.f); //stores the ground height under the ball for the shadows to read
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity ent, float)
    {
        auto state = ent.getComponent<Ball>().state;
        auto pos = ent.getComponent<cro::Transform>().getPosition();
        
        if (state == Ball::State::Flight)
        {
            //update pin distance on ui
            cro::Command cmd;
            cmd.targetFlags = CommandID::UI::PinDistance;
            cmd.action = [&, pos](cro::Entity e, float)
            {
                //if we're on the green convert to cm
                float ballDist = 
                    glm::length(pos - m_holeData[m_gameScene.getDirector<DrivingRangeDirector>()->getCurrentHole()].pin);

                formatDistanceString(ballDist, e.getComponent<cro::Text>(), m_sharedData.imperialMeasurements);

                auto bounds = cro::Text::getLocalBounds(e);
                bounds.width = std::floor(bounds.width / 2.f);
                e.getComponent<cro::Transform>().setOrigin({ bounds.width, 0.f });
            };
            m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

            //and mini-ball in overhead map
            cmd.targetFlags = CommandID::UI::MiniBall;
            cmd.action =
                [pos](cro::Entity e, float)
            {
                auto position = glm::vec3(pos.x, -pos.z, 0.1f) / 2.f;
                //need to tie into the fact the mini map is 1/2 scale
                //and has the origin in the centre
                e.getComponent<cro::Transform>().setPosition(position + glm::vec3(RangeSize / 4.f, 0.f));

                //set scale based on height
                static constexpr float MaxHeight = 40.f;
                float scale = 1.f + ((pos.y / MaxHeight) * 2.f);
                e.getComponent<cro::Transform>().setScale(glm::vec2(scale));
            };
            m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

            //following cameras
            cmd.targetFlags = CommandID::SpectatorCam;
            cmd.action = [&, ent](cro::Entity e, float)
            {
                e.getComponent<CameraFollower>().target = ent;
                e.getComponent<CameraFollower>().playerPosition = PlayerPosition;
                e.getComponent<CameraFollower>().holePosition = m_holeData[m_gameScene.getDirector<DrivingRangeDirector>()->getCurrentHole()].pin;
            };
            m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

        }
        pos.y = 3.f;
        auto groundHeight = m_gameScene.getSystem<BallSystem>()->getTerrain(pos).intersection.y;
        ent.getComponent<cro::Callback>().getUserData<float>() = groundHeight;

        ent.getComponent<ClientCollider>().state = static_cast<std::uint8_t>(state);
    };

    //ball shadow
    auto ballEnt = entity;
    material.setProperty("u_colour", cro::Colour::White);
    material.blendMode = cro::Material::BlendMode::Multiply;

    //point shadow seen from distance
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(PlayerPosition);
    entity.addComponent<cro::Model>(m_resources.meshes.getMesh(shadowMeshID), material);
    entity.getComponent<cro::Model>().setRenderFlags(~RenderFlags::MiniMap);
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&,ballEnt](cro::Entity e, float)
    {
        float height = ballEnt.getComponent<cro::Callback>().getUserData<float>();
        height -= ballEnt.getComponent<cro::Transform>().getPosition().y;
        e.getComponent<cro::Transform>().setPosition({ 0.f, height + 0.003f, 0.f });
    };
    ballEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //large shadow seen close up
    auto shadowEnt = entity;
    entity = m_gameScene.createEntity();
    shadowEnt.getComponent<cro::Transform>().addChild(entity.addComponent<cro::Transform>());
    
    cro::ModelDefinition md(m_resources);
    md.loadFromFile("assets/golf/models/ball_shadow.cmt");
    md.createModel(entity);

    entity.getComponent<cro::Model>().setRenderFlags(~RenderFlags::MiniMap);
    entity.getComponent<cro::Transform>().setScale(glm::vec3(1.3f));

    //adding a ball model means we see something a bit more reasonable when close up
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>();

    if (m_ballModels.count(ballID) != 0)
    {
        m_ballModels[ballID]->createModel(entity);
    }
    else
    {
        //a bit dangerous assuming we're not empty, but we
        //shouldn't have made it this far without loading at least something...
        LogW << "Ball with ID " << (int)ballID << " not found" << std::endl;
        m_ballModels.begin()->second->createModel(entity);
    }

    entity.getComponent<cro::Model>().setMaterial(0, m_resources.materials.get(m_materialIDs[MaterialID::Cel]));
    entity.getComponent<cro::Model>().setRenderFlags(~RenderFlags::MiniMap);
    ballEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

#ifdef CRO_DEBUG_
    ballEntity = ballEnt;
#endif
}

void DrivingState::createFlag()
{
    cro::ModelDefinition md(m_resources);
    md.loadFromFile("assets/golf/models/cup.cmt");
    auto entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setScale({ 1.1f, 1.f, 1.1f });
    md.createModel(entity);

    auto holeEntity = entity;

    md.loadFromFile("assets/golf/models/flag.cmt");
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Flag;
    entity.addComponent<float>() = 0.f;
    md.createModel(entity);
    if (md.hasSkeleton())
    {
        entity.getComponent<cro::Skeleton>().play(0);
    }
    
    auto flagEntity = entity;

    md.loadFromFile("assets/golf/models/beacon.cmt");
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::Transform>().setScale(glm::vec3(0.f));
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Beacon;
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [](cro::Entity e, float dt)
    {
        e.getComponent<cro::Transform>().rotate(cro::Transform::Y_AXIS, dt);
    };
    md.createModel(entity);

    auto beaconMat = m_resources.materials.get(m_materialIDs[MaterialID::Beacon]);
    applyMaterialData(md, beaconMat);

    entity.getComponent<cro::Model>().setMaterial(0, beaconMat);
    entity.getComponent<cro::Model>().setHidden(!m_sharedData.showBeacon);
    entity.getComponent<cro::Model>().setMaterialProperty(0, "u_colourRotation", m_sharedData.beaconColour);
    entity.getComponent<cro::Model>().setMaterialProperty(0, "u_colour", cro::Colour(0.3f, 0.3f, 0.3f));
    auto beaconEntity = entity;



    //draw the flag pole as a single line which can be
    //see from a distance - hole and model are also attached to this
    auto material = m_resources.materials.get(m_materialIDs[MaterialID::WireframeCulled]);
    material.setProperty("u_colour", cro::Colour::White);
    auto meshID = m_resources.meshes.loadMesh(cro::DynamicMeshBuilder(cro::VertexProperty::Position | cro::VertexProperty::Colour, 1, GL_LINE_STRIP));
    entity = m_gameScene.createEntity();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Hole;
    entity.addComponent<cro::Model>(m_resources.meshes.getMesh(meshID), material);
    entity.addComponent<cro::Transform>().setPosition({ 0.f, -FlagCallbackData::MaxDepth, 0.f });
    entity.getComponent<cro::Transform>().addChild(holeEntity.getComponent<cro::Transform>());
    entity.getComponent<cro::Transform>().addChild(flagEntity.getComponent<cro::Transform>());
    entity.getComponent<cro::Transform>().addChild(beaconEntity.getComponent<cro::Transform>());

    auto *meshData = &entity.getComponent<cro::Model>().getMeshData();
    auto vertStride = (meshData->vertexSize / sizeof(float));
    std::vector<float> verts =
    {
        0.f, 2.f, 0.f,      LeaderboardTextLight.getRed(), LeaderboardTextLight.getGreen(), LeaderboardTextLight.getBlue(), 1.f,
        0.f, 1.66f, 0.f,    LeaderboardTextLight.getRed(), LeaderboardTextLight.getGreen(), LeaderboardTextLight.getBlue(), 1.f,

        0.f, 1.66f, 0.f,    0.05f, 0.043f, 0.05f, 1.f,
        0.f, 1.33f, 0.f,    0.05f, 0.043f, 0.05f, 1.f,

        0.f, 1.33f, 0.f,    LeaderboardTextLight.getRed(), LeaderboardTextLight.getGreen(), LeaderboardTextLight.getBlue(), 1.f,
        0.f, 1.f, 0.f,      LeaderboardTextLight.getRed(), LeaderboardTextLight.getGreen(), LeaderboardTextLight.getBlue(), 1.f,

        0.f, 1.f, 0.f,      0.05f, 0.043f, 0.05f, 1.f,
        0.f, 0.66f, 0.f,    0.05f, 0.043f, 0.05f, 1.f,

        0.f, 0.66f, 0.f,    LeaderboardTextLight.getRed(), LeaderboardTextLight.getGreen(), LeaderboardTextLight.getBlue(), 1.f,
        0.f, 0.33f, 0.f,    LeaderboardTextLight.getRed(), LeaderboardTextLight.getGreen(), LeaderboardTextLight.getBlue(), 1.f,

        0.f, 0.33f, 0.f,    0.05f, 0.043f, 0.05f, 1.f,
        0.f, 0.f, 0.f,      0.05f, 0.043f, 0.05f, 1.f,
    };
    std::vector<std::uint32_t> indices =
    {
        0,1,2,3,4,5,6,7,8,9,10,11,12
    };
    meshData->vertexCount = verts.size() / vertStride;
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, meshData->vbo));
    glCheck(glBufferData(GL_ARRAY_BUFFER, meshData->vertexSize * meshData->vertexCount, verts.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ARRAY_BUFFER, 0));

    auto * submesh = &meshData->indexData[0];
    submesh->indexCount = static_cast<std::uint32_t>(indices.size());
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, submesh->ibo));
    glCheck(glBufferData(GL_ELEMENT_ARRAY_BUFFER, submesh->indexCount * sizeof(std::uint32_t), indices.data(), GL_STATIC_DRAW));
    glCheck(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    entity.addComponent<cro::ParticleEmitter>().settings.loadFromFile("assets/golf/particles/flag.cps", m_resources.textures);
    entity.addComponent<cro::Callback>().setUserData<FlagCallbackData>();
    entity.getComponent<cro::Callback>().function =
        [&, beaconEntity](cro::Entity e, float dt) mutable
    {
        auto& data = e.getComponent<cro::Callback>().getUserData<FlagCallbackData>();
        auto& tx = e.getComponent<cro::Transform>();
        if (data.state == FlagCallbackData::Out)
        {
            data.progress = std::min(1.f, data.progress + dt);

            auto pos = data.startPos;
            pos.y -= FlagCallbackData::MaxDepth * cro::Util::Easing::easeInOutQuint(data.progress);
            tx.setPosition(pos);

            if (data.progress == 1)
            {
                data.state = FlagCallbackData::In;
            }
        }
        else
        {
            data.progress = std::max(0.f, data.progress - dt);

            auto pos = data.targetPos;
            pos.y -= FlagCallbackData::MaxDepth * cro::Util::Easing::easeInOutQuint(data.progress);
            tx.setPosition(pos);

            if (data.progress == 0)
            {
                data.state = FlagCallbackData::Out;
                data.startPos = data.targetPos;

                e.getComponent<cro::Callback>().active = false;
                e.getComponent<cro::ParticleEmitter>().start();

                m_inputParser.setActive(true);

                //show the input bar
                cro::Command cmd;
                cmd.targetFlags = CommandID::UI::Root;
                cmd.action = [](cro::Entity f, float)
                {
                    f.getComponent<cro::Callback>().getUserData<std::pair<std::int32_t, float>>().first = 0;
                    f.getComponent<cro::Callback>().active = true;
                };
                m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

                cmd.targetFlags = CommandID::StrokeArc;
                cmd.action = [](cro::Entity f, float)
                {
                    f.getComponent<cro::Callback>().active = true;
                };
                m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
            }
        }

        //update beacon if active
        if (m_sharedData.showBeacon)
        {
            float scale = 1.f - data.progress;
            beaconEntity.getComponent<cro::Transform>().setScale({ scale, scale, scale });
        }
    };
}

void DrivingState::startTransition()
{
    auto entity = m_gameScene.createEntity();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.5f);
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& currTime = e.getComponent<cro::Callback>().getUserData<float>();
        currTime -= dt;
        if (currTime < 0)
        {
            m_cameras[CameraID::Player].getComponent<cro::Callback>().active = true;

            e.getComponent<cro::Callback>().active = false;
            m_gameScene.destroyEntity(e);
        }
    };    
    

    //scanlines drawn over the UI
    glm::vec2 screenSize(cro::App::getWindow().getSize());
    auto& shader = m_resources.shaders.get(ShaderID::Transition);

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, 0.f, 2.f });
    entity.addComponent<cro::Drawable2D>().setShader(&shader);
    entity.getComponent<cro::Drawable2D>().setVertexData(
        {
            cro::Vertex2D(glm::vec2(0.f, screenSize.y), glm::vec2(0.f, 1.f), cro::Colour::Black),
            cro::Vertex2D(glm::vec2(0.f), glm::vec2(0.f), cro::Colour::Black),
            cro::Vertex2D(screenSize, glm::vec2(1.f), cro::Colour::Black),
            cro::Vertex2D(glm::vec2(screenSize.x, 0.f), glm::vec2(1.f, 0.f), cro::Colour::Black)
        });

    auto timeID = shader.getUniformID("u_time");
    auto shaderID = shader.getGLHandle();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function =
        [&, shaderID, timeID](cro::Entity e, float dt)
    {
        static constexpr float MaxTime = 2.f - (1.f / 60.f);
        auto& currTime = e.getComponent<cro::Callback>().getUserData<float>();
        currTime = std::min(MaxTime, currTime + dt);

        glCheck(glUseProgram(shaderID));
        glCheck(glUniform1f(timeID, currTime));

        if (currTime == MaxTime)
        {
            e.getComponent<cro::Callback>().active = false;
            m_uiScene.destroyEntity(e);
        }
    };

    glCheck(glUseProgram(shader.getGLHandle()));
    glCheck(glUniform2f(shader.getUniformID("u_scale"), m_viewScale.x, m_viewScale.y));
    glCheck(glUniform2f(shader.getUniformID("u_resolution"), screenSize.x, screenSize.y));
}

void DrivingState::hitBall()
{
    auto pitch = Clubs[m_inputParser.getClub()].angle;

    auto yaw = m_inputParser.getYaw();

    //add hook/slice to yaw
    auto hook = m_inputParser.getHook();
    //changing this func changes how accurate a player needs to be
    //sine, quad, cubic, quart, quint in steepness order
    if (Achievements::getActive())
    {
        auto s = cro::Util::Maths::sgn(hook);
        auto level = Social::getLevel();
        switch (level / 25)
        {
        default:
            hook = cro::Util::Easing::easeOutQuint(hook * s) * s;
            break;
        case 3:
            hook = cro::Util::Easing::easeOutQuart(hook * s) * s;
            break;
        case 2:
            hook = cro::Util::Easing::easeOutCubic(hook * s) * s;
            break;
        case 1:
            hook = cro::Util::Easing::easeOutQuad(hook * s) * s;
            break;
        case 0:
            hook = cro::Util::Easing::easeOutSine(hook * s) * s;
            break;
        }
    }
    yaw += MaxHook * hook;

    yaw += cro::Util::Const::PI / 2.f; //can't remember why we have to do this - probably to do with cam rotation in the main mode. This fudges it though.

    glm::vec3 impulse(1.f, 0.f, 0.f);
    auto rotation = glm::rotate(glm::quat(1.f, 0.f, 0.f, 0.f), yaw, cro::Transform::Y_AXIS);
    rotation = glm::rotate(rotation, pitch, cro::Transform::Z_AXIS);
    impulse = glm::toMat3(rotation) * impulse;

    impulse *= Clubs[m_inputParser.getClub()].power * cro::Util::Easing::easeOutSine(m_inputParser.getPower());
    impulse *= Dampening[TerrainID::Fairway];

    //apply impulse to ball component
    cro::Command cmd;
    cmd.targetFlags = CommandID::Ball;
    cmd.action = [impulse](cro::Entity e, float)
    {
        auto& ball = e.getComponent<Ball>();

        if (ball.state == Ball::State::Idle)
        {
            ball.velocity = impulse;
            ball.state = Ball::State::Flight;
            ball.delay = 0.f;
            ball.startPoint = e.getComponent<cro::Transform>().getPosition();
        }
    };
    m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);



    //relay this message with the info needed for particle/sound effects
    auto* msg = cro::App::getInstance().getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
    msg->type = GolfEvent::ClubSwing;
    msg->position = PlayerPosition;
    msg->terrain = TerrainID::Fairway;
    msg->club = static_cast<std::uint8_t>(m_inputParser.getClub());

    float lowFreq = 50000.f * m_inputParser.getPower() * m_sharedData.enableRumble;
    float hiFreq = 35000.f * m_inputParser.getPower() * m_sharedData.enableRumble;

    cro::GameController::rumbleStart(activeControllerID(m_sharedData.inputBinding.playerID), static_cast<std::uint16_t>(lowFreq), static_cast<std::uint16_t>(hiFreq), 200);

    //from here the hook value is just used for UI feedback
    //so we want to flip it as appropriate with the current avatar
    hook *= playerXScale;

    //check if we hooked/sliced
    if (hook < -0.15f)
    {
        auto* msg2 = cro::App::getInstance().getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
        msg2->type = GolfEvent::HookedBall;
        floatingMessage("Hook");
    }
    else if (hook > 0.15f)
    {
        auto* msg2 = cro::App::getInstance().getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
        msg2->type = GolfEvent::SlicedBall;
        floatingMessage("Slice");
    }

    auto power = m_inputParser.getPower();
    hook *= 20.f;
    hook = std::round(hook);
    hook /= 20.f;

    if (power > 0.9f
        && std::fabs(hook) < 0.05f)
    {
        auto* msg2 = cro::App::getInstance().getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
        msg2->type = GolfEvent::NiceShot;
    }

    //hide the power bar
    cmd.targetFlags = CommandID::UI::Root;
    cmd.action = [](cro::Entity e, float)
    {
        e.getComponent<cro::Callback>().getUserData<std::pair<std::int32_t, float>>().first = 1;
        e.getComponent<cro::Callback>().active = true;
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    cmd.targetFlags = CommandID::StrokeArc;
    cmd.action = [](cro::Entity e, float)
    {
        e.getComponent<cro::Callback>().active = true;
    };
    m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
}

void DrivingState::setHole(std::int32_t index)
{
    m_gameScene.getSystem<BallSystem>()->setHoleData(m_holeData[index], false);
    m_inputParser.resetPower();
    //activated when flag anim finishes


    //reset avatar
    cro::Command cmd;
    cmd.targetFlags = CommandID::PlayerAvatar;
    cmd.action = [&](cro::Entity e, float)
    {
        e.getComponent<cro::Skeleton>().play(m_avatar.animationIDs[AnimationID::Idle]);
    };
    m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //update club text colour based on distance
    cmd.targetFlags = CommandID::UI::ClubName;
    cmd.action = [&, index](cro::Entity e, float)
    {
        e.getComponent<cro::Text>().setString(Clubs[m_inputParser.getClub()].getName(m_sharedData.imperialMeasurements));

        auto dist = glm::length(PlayerPosition - m_holeData[index].pin) * 1.67f;
        if (m_inputParser.getClub() < ClubID::NineIron &&
            Clubs[m_inputParser.getClub()].target > dist)
        {
            e.getComponent<cro::Text>().setFillColour(TextHighlightColour);
        }
        else
        {
            e.getComponent<cro::Text>().setFillColour(TextNormalColour);
        }
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //reset ball position
    cmd.targetFlags = CommandID::Ball;
    cmd.action = [](cro::Entity e, float)
    {
        e.getComponent<cro::Transform>().setPosition(PlayerPosition);
    };
    m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //trigger flag animation
    cmd.targetFlags = CommandID::Hole;
    cmd.action = [&, index](cro::Entity e, float)
    {
        e.getComponent<cro::Callback>().getUserData<FlagCallbackData>().targetPos = m_holeData[index].pin;
        e.getComponent<cro::Callback>().active = true;
    };
    m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //update distance to hole
    cmd.targetFlags = CommandID::UI::PinDistance;
    cmd.action = [&, index](cro::Entity e, float)
    {
        float ballDist = glm::length(PlayerPosition - m_holeData[index].pin);
        formatDistanceString(ballDist, e.getComponent<cro::Text>(), m_sharedData.imperialMeasurements);

        auto bounds = cro::Text::getLocalBounds(e);
        bounds.width = std::floor(bounds.width / 2.f);
        e.getComponent<cro::Transform>().setOrigin({ bounds.width, 0.f });
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //and turn indicator
    cmd.targetFlags = CommandID::UI::HoleNumber;
    cmd.action = [&](cro::Entity e, float)
    {
        std::string str("Turn ");
        str += std::to_string(m_gameScene.getDirector<DrivingRangeDirector>()->getCurrentStroke() + 1);
        str += " of ";
        str += std::to_string(m_strokeCounts[m_strokeCountIndex]);
        e.getComponent<cro::Text>().setString(str);
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //update the minimap
    cmd.targetFlags = CommandID::UI::MiniMap;
    cmd.action = [](cro::Entity e, float)
    {
        e.getComponent<cro::Callback>().active = true;
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //reset mini ball
    cmd.targetFlags = CommandID::UI::MiniBall;
    cmd.action =
        [](cro::Entity e, float)
    {
        auto pos = glm::vec3(PlayerPosition.x, -PlayerPosition.z, 0.1f);
        e.getComponent<cro::Transform>().setPosition(pos / 2.f);
        e.getComponent<cro::Transform>().move(RangeSize / 4.f);

        //play the callback animation
        e.getComponent<cro::Callback>().active = true;
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    
    auto holePos = m_holeData[index].pin;

    //reposition sky cam based on target position
    auto camPos = m_cameras[CameraID::Sky].getComponent<cro::Transform>().getPosition();
    camPos.x = holePos.x > 0 ? RangeSize.x / 3.f : -RangeSize.x / 3.f;
    auto tx = glm::inverse(glm::lookAt(camPos, PlayerPosition, cro::Transform::Y_AXIS));
    m_cameras[CameraID::Sky].getComponent<cro::Transform>().setLocalTransform(tx);

    //reposition the green-cam
    //TODO interp the motion?
    m_cameras[CameraID::Green].getComponent<cro::Transform>().setPosition({ holePos.x, GreenCamHeight, holePos.z });

    //always away from tee
    auto direction = holePos - PlayerPosition;
    direction = glm::normalize(direction) * 15.f;
    m_cameras[CameraID::Green].getComponent<cro::Transform>().move(direction);


    //double check terrain height
    auto result = m_gameScene.getSystem<BallSystem>()->getTerrain(m_cameras[CameraID::Green].getComponent<cro::Transform>().getPosition());
    result.intersection.y = std::max(result.intersection.y, holePos.y);
    result.intersection.y += GreenCamHeight;
    
    tx = glm::inverse(glm::lookAt(result.intersection, m_holeData[index].pin, cro::Transform::Y_AXIS));
    m_cameras[CameraID::Green].getComponent<cro::Transform>().setLocalTransform(tx);

    m_gameScene.setSystemActive<CameraFollowSystem>(false);

    //TODO do we only want this to happen if we're on random holes?
    m_gameScene.getSystem<BallSystem>()->forceWindChange();
}

void DrivingState::setActiveCamera(std::int32_t camID)
{
#ifdef CRO_DEBUG_
    if (useFreeCam)
    {
        return;
    }
#endif

    CRO_ASSERT(camID >= 0 && camID < CameraID::Count, "");

    if (m_cameras[camID].isValid()
        && camID != m_currentCamera)
    {
        if (camID != CameraID::Player
            && (camID < m_currentCamera))
        {
            //don't switch back to the previous camera
            //ie if we're on the green cam don't switch
            //back to sky
            return;
        }

        m_cameras[m_currentCamera].getComponent<cro::Camera>().active = false;

        //set scene camera
        m_gameScene.setActiveCamera(m_cameras[camID]);
        m_gameScene.setActiveListener(m_cameras[camID]);
        m_currentCamera = camID;

        m_cameras[m_currentCamera].getComponent<cro::Camera>().active = true;
    }
}

void DrivingState::loadScores()
{
    Social::readDrivingStats(m_topScores);
}

void DrivingState::saveScores()
{
    Social::storeDrivingStats(m_topScores);
}