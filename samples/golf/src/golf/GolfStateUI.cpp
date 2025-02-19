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

#include "GolfState.hpp"
#include "GameConsts.hpp"
#include "CommandIDs.hpp"
#include "SharedStateData.hpp"
#include "Clubs.hpp"
#include "MenuConsts.hpp"
#include "CommonConsts.hpp"
#include "TextAnimCallback.hpp"
#include "ScoreStrings.hpp"
#include "MessageIDs.hpp"
#include "NotificationSystem.hpp"
#include "TrophyDisplaySystem.hpp"
#include "FloatingTextSystem.hpp"
#include "PacketIDs.hpp"
#include "MiniBallSystem.hpp"
#include "../ErrorCheck.hpp"

#include <Achievements.hpp>
#include <AchievementStrings.hpp>
#include <Social.hpp>

#include <crogine/audio/AudioScape.hpp>

#include <crogine/ecs/components/Transform.hpp>
#include <crogine/ecs/components/Sprite.hpp>
#include <crogine/ecs/components/SpriteAnimation.hpp>
#include <crogine/ecs/components/Text.hpp>
#include <crogine/ecs/components/Drawable2D.hpp>
#include <crogine/ecs/components/Camera.hpp>
#include <crogine/ecs/components/CommandTarget.hpp>
#include <crogine/ecs/components/Callback.hpp>
#include <crogine/ecs/components/ParticleEmitter.hpp>

#include <crogine/ecs/systems/SpriteSystem3D.hpp>
#include <crogine/ecs/systems/RenderSystem2D.hpp>
#include <crogine/ecs/systems/CameraSystem.hpp>

#include <crogine/graphics/SpriteSheet.hpp>

#include <crogine/util/Easings.hpp>
#include <crogine/util/Maths.hpp>
#include <crogine/util/Random.hpp>

namespace
{
#include "PostProcess.inl"

    static constexpr float ColumnWidth = 20.f;
    static constexpr float ColumnHeight = 276.f;
    static constexpr float ColumnMargin = 6.f;
    static constexpr std::array ColumnPositions =
    {
        glm::vec2(8.f, ColumnHeight),
        glm::vec2((ColumnWidth * 6.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 7.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 8.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 9.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 10.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 11.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 12.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 13.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 14.f) + ColumnMargin, ColumnHeight),
        glm::vec2((ColumnWidth * 15.f) + ColumnMargin, ColumnHeight),
    };

    static constexpr float MaxExpansion = 100.f;
    float scoreboardExpansion = 0.f; //TODO move to member
    float stretchToScreen(cro::Entity e, cro::Sprite sprite, glm::vec2 baseSize)
    {
        constexpr float EdgeOffset = 40.f; //this much from outside before splitting

        e.getComponent<cro::Drawable2D>().setTexture(sprite.getTexture());
        auto bounds = sprite.getTextureBounds();
        auto rect = sprite.getTextureRectNormalised();

        //how much bigger to get either side in wider views
        float expansion = std::min(MaxExpansion, std::floor((baseSize.x - bounds.width) / 2.f));
        //only needs > 0 really but this gives a little leeway
        expansion = (baseSize.x - bounds.width > 10) ? expansion : 0.f;
        float edgeOffsetNorm = (EdgeOffset / sprite.getTexture()->getSize().x);

        bounds.width += expansion * 2.f;

        e.getComponent<cro::Drawable2D>().setVertexData(
            {
                cro::Vertex2D(glm::vec2(0.f, bounds.height), glm::vec2(rect.left, rect.bottom + rect.height)),
                cro::Vertex2D(glm::vec2(0.f), glm::vec2(rect.left, rect.bottom)),

                cro::Vertex2D(glm::vec2(EdgeOffset, bounds.height), glm::vec2(rect.left + edgeOffsetNorm, rect.bottom + rect.height)),
                cro::Vertex2D(glm::vec2(EdgeOffset, 0.f), glm::vec2(rect.left + edgeOffsetNorm, rect.bottom)),
                cro::Vertex2D(glm::vec2(EdgeOffset + expansion, bounds.height), glm::vec2(rect.left + edgeOffsetNorm, rect.bottom + rect.height)),
                cro::Vertex2D(glm::vec2(EdgeOffset + expansion, 0.f), glm::vec2(rect.left + edgeOffsetNorm, rect.bottom)),


                cro::Vertex2D(glm::vec2(bounds.width - EdgeOffset - expansion, bounds.height), glm::vec2((rect.left + rect.width) - edgeOffsetNorm, rect.bottom + rect.height)),
                cro::Vertex2D(glm::vec2(bounds.width - EdgeOffset - expansion, 0.f), glm::vec2((rect.left + rect.width) - edgeOffsetNorm, rect.bottom)),
                cro::Vertex2D(glm::vec2(bounds.width - EdgeOffset, bounds.height), glm::vec2((rect.left + rect.width) - edgeOffsetNorm, rect.bottom + rect.height)),
                cro::Vertex2D(glm::vec2(bounds.width - EdgeOffset, 0.f), glm::vec2((rect.left + rect.width) - edgeOffsetNorm, rect.bottom)),


                cro::Vertex2D(glm::vec2(bounds.width, bounds.height), glm::vec2(rect.left + rect.width, rect.bottom + rect.height)),
                cro::Vertex2D(glm::vec2(bounds.width, 0.f), glm::vec2(rect.left + rect.width, rect.bottom))
            });

        return expansion;
    }
}

void GolfState::buildUI()
{
    if (m_holeData.empty())
    {
        return;
    }

    auto resizeCallback = [&](cro::Entity e, float)
    {
        //this is activated once to make sure the
        //sprite is up to date with any texture buffer resize
        glm::vec2 texSize = e.getComponent<cro::Sprite>().getTexture()->getSize();
        e.getComponent<cro::Sprite>().setTextureRect({ glm::vec2(0.f), texSize });
        e.getComponent<cro::Transform>().setOrigin(texSize / 2.f);
        e.getComponent<cro::Callback>().active = false;
    };

    //draws the background using the render texture
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>(m_gameSceneTexture.getTexture());
    auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec3(bounds.width / 2.f, bounds.height / 2.f, 0.5f));
    entity.addComponent<cro::Callback>().function = resizeCallback;

    auto courseEnt = entity;
    m_courseEnt = courseEnt;

    /*m_resources.shaders.loadFromString(ShaderID::FXAA, FXAAVertex, FXAAFrag);
    auto& shader = m_resources.shaders.get(ShaderID::FXAA);
    m_courseEnt.getComponent<cro::Drawable2D>().setShader(&shader);*/

    //displays the trophies on round end - has to be displayed over top of scoreboard
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>(m_trophySceneTexture.getTexture());
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec3(bounds.width / 2.f, bounds.height / 2.f, 0.f));
    entity.addComponent<cro::Callback>().function = resizeCallback;

    auto trophyEnt = entity;
    for (auto label : m_trophyLabels)
    {
        trophyEnt.getComponent<cro::Transform>().addChild(label.getComponent<cro::Transform>());
        label.getComponent<cro::Callback>().setUserData<cro::Entity>(trophyEnt);
    }

    //info panel background - vertices are set in resize callback
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    auto infoEnt = entity;
    createSwingMeter(infoEnt);
    
    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

    //player's name
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::PlayerName | CommandID::UI::UIElement;
    entity.addComponent<UIElement>().relativePosition = { 0.2f, 0.f };
    entity.getComponent<UIElement>().depth = 0.05f;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    entity.addComponent<cro::Callback>().setUserData<TextCallbackData>();
    entity.getComponent<cro::Callback>().function = TextAnimCallback();
    auto nameEnt = entity;
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());



    //think bulb displayed when CPU players are thinking
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setScale({ 0.f, 0.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::Thinking];
    bounds = m_sprites[SpriteID::Thinking].getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    entity.addComponent<cro::SpriteAnimation>().play(0);
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::ThinkBubble;
    entity.addComponent<cro::Callback>().setUserData<std::pair<std::int32_t, float>>(1, 0.f);
    entity.getComponent<cro::Callback>().function =
        [nameEnt](cro::Entity e, float dt)
        {
            auto end = nameEnt.getComponent<cro::Drawable2D>().getLocalBounds().width;
            e.getComponent<cro::Transform>().setPosition({ end + 6.f, 4.f });

            float scale = 0.f;
            auto& [direction, currTime] = e.getComponent<cro::Callback>().getUserData<std::pair<std::int32_t, float>>();
            if (direction == 0)
            {
                //grow
                currTime = std::min(1.f, currTime + (dt * 3.f));
                scale = cro::Util::Easing::easeOutQuint(currTime);
            }
            else
            {
                //shrink
                currTime = std::max(0.f, currTime - (dt * 3.f));
                if (currTime == 0)
                {
                    e.getComponent<cro::Callback>().active = false;
                }

                scale = cro::Util::Easing::easeInQuint(currTime);
            }
            
            e.getComponent<cro::Transform>().setScale({ scale, scale });
        };
    nameEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //hole distance
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::PinDistance | CommandID::UI::UIElement;
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 1.f };
    entity.getComponent<UIElement>().depth = 0.05f;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //club info
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::ClubName | CommandID::UI::UIElement;
    entity.addComponent<UIElement>().relativePosition = ClubTextPosition;
    entity.getComponent<UIElement>().depth = 0.05f;
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //current stroke
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;
    entity.addComponent<UIElement>().relativePosition = { 0.61f, 0.f };
    entity.getComponent<UIElement>().depth = 0.05f;
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float)
    {
        auto stroke = std::to_string(m_sharedData.connectionData[m_currentPlayer.client].playerData[m_currentPlayer.player].holeScores[m_currentHole]);
        auto par = std::to_string(m_holeData[m_currentHole].par);
        e.getComponent<cro::Text>().setString("Score: " + stroke + " - Par: " + par);
    };
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());


    //current terrain
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;
    entity.addComponent<UIElement>().relativePosition = { 0.76f, 1.f };
    entity.getComponent<UIElement>().depth = 0.05f;
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float)
    {
        e.getComponent<cro::Text>().setString(TerrainStrings[m_currentPlayer.terrain]);
    };
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());



    //wind strength - this is positioned by the camera's resize callback, below
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::WindString;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    auto windEnt = entity;

    //wind indicator
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(glm::vec3(38.f, 20.f, 0.03f));
    entity.addComponent<cro::Drawable2D>().setVertexData( 
    {
        cro::Vertex2D(glm::vec2(-1.f, 12.f), LeaderboardTextLight),
        cro::Vertex2D(glm::vec2(-1.f, 0.f), LeaderboardTextLight),
        cro::Vertex2D(glm::vec2(1.f, 12.f), LeaderboardTextLight),
        cro::Vertex2D(glm::vec2(1.f, 0.f), LeaderboardTextLight)
    });
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::WindSock;
    entity.addComponent<float>() = 0.f; //current wind direction/rotation
    windEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(glm::vec3(38.f, 52.f, 0.01f));
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::WindIndicator];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec2(bounds.width / 2.f, bounds.height / 2.f));
    entity.getComponent<cro::Transform>().move(glm::vec2(0.f, -bounds.height));
    windEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    auto windDial = entity;
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::WindSpeed];
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::WindSpeed;
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function =
        [](cro::Entity e, float)
    {
        auto speed = e.getComponent<cro::Callback>().getUserData<float>();
        e.getComponent<cro::Transform>().rotate(speed / 6.f);
    };
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec3(bounds.width / 2.f, bounds.height / 2.f, 0.01f));
    entity.getComponent<cro::Transform>().setPosition(windDial.getComponent<cro::Transform>().getOrigin());
    windDial.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    
    //sets the initial cam rotation for the wind indicator compensation
    auto camDir = m_holeData[0].target - m_currentPlayer.position;
    m_camRotation = std::atan2(-camDir.z, camDir.y);

    //root used to show/hide input UI
    auto rootNode = m_uiScene.createEntity();
    rootNode.addComponent<cro::Transform>().setScale(glm::vec2(0.f));
    rootNode.addComponent<cro::CommandTarget>().ID = CommandID::UI::Root;
    rootNode.addComponent<cro::Callback>().setUserData<std::pair<std::int32_t, float>>(0, 0.f);
    rootNode.getComponent<cro::Callback>().function =
        [](cro::Entity e, float dt)
    {
        auto& [dir, currTime] = e.getComponent<cro::Callback>().getUserData<std::pair<std::int32_t, float>>();

        if (dir == 0)
        {
            //grow
            currTime = std::min(1.f, currTime + dt);
            const float scale = cro::Util::Easing::easeOutElastic(currTime);

            e.getComponent<cro::Transform>().setScale({ scale, scale });

            if (currTime == 1)
            {
                dir = 1;
                e.getComponent<cro::Callback>().active = false;
            }
        }
        else
        {
            //shrink
            currTime = std::max(0.f, currTime - (dt * 2.f));
            const float scale = cro::Util::Easing::easeOutBack(currTime);

            e.getComponent<cro::Transform>().setScale({ scale, 1.f });

            if (currTime == 0)
            {
                dir = 0;
                e.getComponent<cro::Callback>().active = false;
            }
        }
        
    };
    infoEnt.getComponent<cro::Transform>().addChild(rootNode.getComponent<cro::Transform>());

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PowerBar];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec3(bounds.width / 2.f, bounds.height / 2.f, -0.05f));
    rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //power bar
    auto barEnt = entity;
    const auto BarCentre = bounds.width / 2.f;
    const auto BarWidth = bounds.width - 8.f;
    const auto BarHeight = bounds.height;
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(glm::vec3(5.f, 0.f, 0.05f)); //TODO expell the magic number!!
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PowerBarInner];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, bounds](cro::Entity e, float)
    {
        auto crop = bounds;
        crop.width *= m_inputParser.getPower();
        e.getComponent<cro::Drawable2D>().setCroppingArea(crop);
    };
    barEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //hook/slice indicator
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(glm::vec3(BarCentre, 8.f, 0.1f));
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::HookBar];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec2(bounds.width / 2.f, bounds.height / 2.f));
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, BarCentre](cro::Entity e, float)
    {
        glm::vec3 pos(BarCentre + (BarCentre * m_inputParser.getHook()), 8.f, 0.1f);
        e.getComponent<cro::Transform>().setPosition(pos);
    };
    barEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //flag power/distance when putting
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(glm::vec3(2.f, BarHeight, 0.1f));
    entity.getComponent<cro::Transform>().setOrigin({ -6.f, 1.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::MiniFlag];
    entity.addComponent<cro::SpriteAnimation>().play(0);
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, BarWidth](cro::Entity e, float dt)
    {
        const float vScaleTarget = m_currentPlayer.terrain == TerrainID::Green ? 1.f : 0.f;
        auto scale = e.getComponent<cro::Transform>().getScale();
        if (vScaleTarget > 0)
        {
            //grow
            scale.y = std::min(1.f, scale.y + dt);

            //move to position
            auto maxDist = Clubs[ClubID::Putter].target;
            auto currDist = glm::length(m_currentPlayer.position - m_holeData[m_currentHole].pin);
            float hTarget = (currDist / maxDist) * BarWidth;

            auto pos = e.getComponent<cro::Transform>().getPosition();
            pos.x = std::min(pos.x + ((hTarget - pos.x) * dt), BarWidth - 4.f);
            e.getComponent<cro::Transform>().setPosition(pos);
        }
        else
        {
            //shrink
            scale.y = std::max(0.f, scale.y - dt);
        }
        e.getComponent<cro::Transform>().setScale(scale);
    };
    barEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //hole number
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(glm::vec2(38.f, -12.f));
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::HoleNumber;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    entity.addComponent<cro::Callback>().setUserData<TextCallbackData>();
    entity.getComponent<cro::Callback>().function = TextAnimCallback();
    windEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());


    //minimap view
    struct MinimapData final
    {
        std::int32_t state = 0;
        float scale = 1.f;
        float rotation = -1.f;
    };
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, 82.f });
    entity.getComponent<cro::Transform>().setRotation(-90.f * cro::Util::Const::degToRad);
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::MiniMap;
    entity.addComponent<cro::Callback>().setUserData<MinimapData>();
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& [state, scale, rotation] = e.getComponent<cro::Callback>().getUserData<MinimapData>();
        float speed = dt * 4.f;
        float newScale = 0.f;
        
        if (state == 0)
        {
            //shrinking
            scale = std::max(0.f, scale - speed);
            newScale = cro::Util::Easing::easeOutSine(scale);

            if (scale == 0)
            {
                //orientation - sets tee to bottom of map
                if (m_currentHole == 0
                    || m_holeData[m_currentHole].modelEntity != m_holeData[m_currentHole - 1].modelEntity)
                {
                    if (m_holeData[m_currentHole].tee.x > 160)
                    {
                        e.getComponent<cro::Transform>().setRotation(-90.f * cro::Util::Const::degToRad);
                        rotation = -1.f;
                    }
                    else
                    {
                        e.getComponent<cro::Transform>().setRotation(90.f * cro::Util::Const::degToRad);
                        rotation = 1.f;
                    }
                }
                glm::vec2 offset = glm::vec2(2.f * -rotation);
                m_flagQuad.setRotation(-rotation * 90.f);

                m_mapCam.getComponent<cro::Transform>().setRotation(cro::Transform::X_AXIS, -90.f * cro::Util::Const::degToRad);
                m_minimapRotation = 0.f;

                //zoom in putting/small course
                m_minimapOffset = { 0.f,0.f,0.f };
                auto aabb = m_holeData[m_currentHole].modelEntity.getComponent<cro::Model>().getAABB();
                auto minBounds = (aabb[1].x - aabb[0].x) / 2.f; //if this is a small bounds it's probably a single hole
                auto dist = m_holeData[m_currentHole].pin - m_holeData[m_currentHole].tee;
                if (auto len2 = glm::length2(dist); len2 < (110.f * 110.f) && len2 < (minBounds * minBounds))
                {
                    float padding = std::max(1.f, 
                        (std::sqrt(len2) + glm::length(m_holeData[m_currentHole].target - m_holeData[m_currentHole].tee)) / 10.f) * 5.f;

                    float width = std::abs(dist.x) + padding;
                    float height = std::abs(dist.z) + padding;
                    m_minimapScale = std::max(1.f, std::min(std::floor(static_cast<float>(MapSize.x) / width), static_cast<float>(MapSize.y) / height));

                    if (height > width)
                    {
                        m_minimapRotation = (90.f * cro::Util::Const::degToRad) * -cro::Util::Maths::sgn(dist.x);
                        m_mapCam.getComponent<cro::Transform>().rotate(cro::Transform::Z_AXIS, m_minimapRotation);
                    }

                    m_minimapOffset = (m_holeData[m_currentHole].tee + 
                                        m_holeData[m_currentHole].target + 
                                        m_holeData[m_currentHole].pin) / 3.f;

                    m_minimapOffset -= m_holeData[m_currentHole].modelEntity.getComponent<cro::Transform>().getOrigin();
                    m_mapQuad.setUniform("u_effect", 1.f);
                }
                else
                {
                    float width = aabb[1].x - aabb[0].x;
                    auto height = aabb[1].z - aabb[0].z;
                    m_minimapScale = std::max(1.f, std::min(std::floor(static_cast<float>(MapSize.x) / width), static_cast<float>(MapSize.y) / height));
                    m_mapQuad.setUniform("u_effect", 0.f);
                }

                //update render
                auto oldCam = m_gameScene.setActiveCamera(m_mapCam);
                m_gameScene.getSystem<cro::CameraSystem>()->process(0.f);
                m_holeData[m_currentHole].modelEntity.getComponent<cro::Transform>().setScale(glm::vec3(m_minimapScale));
                m_holeData[m_currentHole].modelEntity.getComponent<cro::Transform>().move(-m_minimapOffset * m_minimapScale);
                m_mapBuffer.clear(cro::Colour::Transparent);
                m_gameScene.render();
                m_mapBuffer.display();
                m_gameScene.setActiveCamera(oldCam);
                m_holeData[m_currentHole].modelEntity.getComponent<cro::Transform>().setScale(glm::vec3(1.f));
                m_holeData[m_currentHole].modelEntity.getComponent<cro::Transform>().move(m_minimapOffset * m_minimapScale);

                m_mapQuad.setPosition(offset);
                m_mapQuad.setColour(DropShadowColour);
                m_mapTexture.clear(cro::Colour::Transparent);
                m_mapQuad.draw();

                m_mapQuad.setPosition(glm::vec2(0.f));
                m_mapQuad.setColour(cro::Colour::White);
                m_mapQuad.draw();

                auto holePos = toMinimapCoords(m_holeData[m_currentHole].pin);
                m_flagQuad.setPosition({ holePos.x, holePos.y });
                m_flagQuad.draw();
                m_mapTexture.display();

                //and set to grow
                state = 1;

                //disable the cam again
                m_mapCam.getComponent<cro::Camera>().active = false;
            }
        }
        else
        {
            //growing
            scale = std::min(1.f, scale + speed);
            newScale = cro::Util::Easing::easeInSine(scale);

            if (scale == 1)
            {
                //stop callback
                state = 0;
                e.getComponent<cro::Callback>().active = false;
            }
        }
        e.getComponent<cro::Transform>().setScale(glm::vec2(1.f, newScale));
    };
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    auto mapEnt = entity;
    m_minimapEnt = entity;



    //stroke indicator
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    auto endColour = TextGoldColour;
    endColour.setAlpha(0.f);
    entity.addComponent<cro::Drawable2D>().getVertexData() =
    {
        cro::Vertex2D(glm::vec2(0.f, 0.5f), TextGoldColour),
        cro::Vertex2D(glm::vec2(0.f), TextGoldColour),
        cro::Vertex2D(glm::vec2(0.5f, 0.5f), endColour),
        cro::Vertex2D(glm::vec2(0.5f, -0.5f), endColour)
    };
    entity.getComponent<cro::Drawable2D>().updateLocalBounds();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float)
    {
        e.getComponent<cro::Transform>().setRotation(m_inputParser.getYaw() - m_minimapRotation);
        e.getComponent<cro::Transform>().setPosition(glm::vec3(toMinimapCoords(m_currentPlayer.position), 0.5f));

        if (!m_inputParser.getActive())
        {
            e.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
        }
        else
        {
            auto club = getClub();
            switch (club)
            {
            default: 
                e.getComponent<cro::Transform>().setScale(glm::vec2(Clubs[club].target * m_minimapScale, 1.f));
                break;
            case ClubID::Putter:
                e.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
                break;
            }
        }
    };
    mapEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //green close up view
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setScale({ 0.f, 0.f }); //position is set in UI cam callback, below
    entity.addComponent<cro::Drawable2D>().setShader(&m_resources.shaders.get(ShaderID::Minimap));
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::MiniGreen;
    entity.addComponent<cro::Sprite>(); //updated by the camera callback with correct texture
    entity.addComponent<cro::Callback>().setUserData<GreenCallbackData>();
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt) mutable
    {
        static constexpr float Speed = 2.f;
        auto& [currTime, state, targetScale] = e.getComponent<cro::Callback>().getUserData<GreenCallbackData>();
        if (state == 0)
        {
            //expand
            currTime = std::min(1.f, currTime + (dt * Speed));
            float scale = cro::Util::Easing::easeOutQuint(currTime) * targetScale;
            e.getComponent<cro::Transform>().setScale({ scale,  targetScale });

            if (currTime == 1)
            {
                state = 1;
                e.getComponent<cro::Callback>().active = false;

                //start the cam view updater
                m_greenCam.getComponent<cro::Callback>().active = true;
            }
        }
        else
        {
            //contract
            currTime = std::max(0.f, currTime - (dt * Speed));
            float scale = cro::Util::Easing::easeOutQuint(currTime) * targetScale;
            e.getComponent<cro::Transform>().setScale({ targetScale, scale });

            if (currTime == 0)
            {
                state = 0;
                e.getComponent<cro::Callback>().active = false;

                m_greenCam.getComponent<cro::Callback>().active = false;
                m_greenCam.getComponent<cro::Camera>().active = false;
            }
        }
    };

    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    auto greenEnt = entity;

    //arrow pointing to player position on the green
    //kinda made redundant now that the slope indicator is no longer visible
    //on the mini view
    /*entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, 0.f, 0.1f });
    entity.addComponent<cro::Drawable2D>().getVertexData() =
    {
        cro::Vertex2D(glm::vec2(42.f, -1.f), TextGoldColour),
        cro::Vertex2D(glm::vec2(42.f, 1.f), TextGoldColour),
        cro::Vertex2D(glm::vec2(32.f, 0.f), TextGoldColour)
    };
    entity.getComponent<cro::Drawable2D>().updateLocalBounds();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function =
        [&, greenEnt](cro::Entity e, float dt)
    {
        if (m_currentPlayer.terrain == TerrainID::Green)
        {
            auto offset = greenEnt.getComponent<cro::Transform>().getOrigin();
            e.getComponent<cro::Transform>().setPosition({ offset.x, offset.y });

            auto& currentRotation = e.getComponent<cro::Callback>().getUserData<float>();
            auto dir = m_currentPlayer.position - m_holeData[m_currentHole].pin;
            auto targetRotation = std::atan2(-dir.z, dir.x);

            float rotation = cro::Util::Maths::shortestRotation(currentRotation, targetRotation) * dt;
            currentRotation += rotation;
            e.getComponent<cro::Transform>().setRotation(currentRotation);
        }
    };
    greenEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());*/

    createScoreboard();


    //set up the overhead cam for the mini map
    auto updateMiniView = [&, mapEnt](cro::Camera& miniCam) mutable
    {
        glm::uvec2 previewSize = MapSize / 2u;

        m_mapBuffer.create(previewSize.x, previewSize.y);
        m_mapQuad.setTexture(m_mapBuffer.getTexture());
        m_mapQuad.setShader(m_resources.shaders.get(ShaderID::MinimapView));
        m_mapTexture.create(previewSize.x, previewSize.y);
        mapEnt.getComponent<cro::Sprite>().setTexture(m_mapTexture.getTexture());
        mapEnt.getComponent<cro::Transform>().setOrigin({ previewSize.x / 2.f, previewSize.y / 2.f });

        glm::vec2 viewSize(MapSize);
        miniCam.setOrthographic(-viewSize.x / 2.f, viewSize.x / 2.f, -viewSize.y / 2.f, viewSize.y / 2.f, -0.1f, 30.f);
        miniCam.viewport = { 0.f, 0.f, 1.f, 1.f };
    };

    m_mapCam = m_gameScene.createEntity();
    m_mapCam.addComponent<cro::Transform>().setPosition({ static_cast<float>(MapSize.x) / 2.f, 10.f, -static_cast<float>(MapSize.y) / 2.f});
    m_mapCam.getComponent<cro::Transform>().rotate(cro::Transform::X_AXIS, -90.f * cro::Util::Const::degToRad);
    auto& miniCam = m_mapCam.addComponent<cro::Camera>();
    miniCam.renderFlags = RenderFlags::MiniMap;
    miniCam.active = false;
    //this is a hack to stop the entire terrain being drawn in shadow
    miniCam.shadowMapBuffer.create(2, 2);
    miniCam.shadowMapBuffer.clear();
    miniCam.shadowMapBuffer.display();
    //miniCam.resizeCallback = updateMiniView; //don't do this on resize as recreating the buffer clears it..
    updateMiniView(miniCam);



    //and the mini view of the green
    auto updateGreenView = [&, greenEnt](cro::Camera& greenCam) mutable
    {
        auto texSize = MapSize.y / 2u;

        auto windowScale = std::floor(cro::App::getWindow().getSize().y / calcVPSize().y);
        float scale = m_sharedData.pixelScale ? windowScale : 1.f;
        scale = (windowScale + 1.f) - scale;
        texSize *= static_cast<std::uint32_t>(scale);

        std::uint32_t samples = m_sharedData.pixelScale ? 0 :
            m_sharedData.antialias ? m_sharedData.multisamples : 0;

        m_greenBuffer.create(texSize, texSize, true, false, samples); //yes, it's square
        greenEnt.getComponent<cro::Sprite>().setTexture(m_greenBuffer.getTexture());

        auto targetScale = glm::vec2(1.f / scale);
        if (m_currentPlayer.terrain == TerrainID::Green)
        {
            greenEnt.getComponent<cro::Transform>().setScale(targetScale);
        }
        greenEnt.getComponent<cro::Transform>().setOrigin({ (texSize / 2), (texSize / 2) }); //must divide to a whole pixel!
        greenEnt.getComponent<cro::Callback>().getUserData<GreenCallbackData>().targetScale = targetScale.x;
    };

    m_greenCam = m_gameScene.createEntity();
    m_greenCam.addComponent<cro::Transform>().rotate(cro::Transform::X_AXIS, -90.f * cro::Util::Const::degToRad);
    auto& greenCam = m_greenCam.addComponent<cro::Camera>();
    greenCam.renderFlags = RenderFlags::MiniGreen;
    greenCam.resizeCallback = updateGreenView;
    greenCam.active = false;
    greenCam.shadowMapBuffer.create(2, 2);
    greenCam.shadowMapBuffer.clear();
    greenCam.shadowMapBuffer.display();
    updateGreenView(greenCam);

    m_greenCam.addComponent<cro::Callback>().active = true;
    m_greenCam.getComponent<cro::Callback>().setUserData<MiniCamData>();
    m_greenCam.getComponent<cro::Callback>().function =
        [](cro::Entity e, float dt)
    {
        //zooms the view as the current player approaches hole
        auto& data = e.getComponent<cro::Callback>().getUserData<MiniCamData>();
        auto diff = data.targetSize - data.currentSize;
        data.currentSize += diff * (dt * 4.f);

        auto& cam = e.getComponent<cro::Camera>();
        cam.setOrthographic(-data.currentSize, data.currentSize, -data.currentSize, data.currentSize, -0.15f, 1.f);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };
    };


    //callback for the UI camera when window is resized
    auto updateView = [&, trophyEnt, courseEnt, infoEnt, windEnt, mapEnt, greenEnt, rootNode](cro::Camera& cam) mutable
    {
        auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
        cam.setOrthographic(0.f, size.x, 0.f, size.y, -3.5f, 20.f);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };

        auto vpSize = calcVPSize();
        m_viewScale = glm::vec2(std::floor(size.y / vpSize.y));
        m_inputParser.setMouseScale(m_viewScale.x);

        glm::vec2 courseScale(m_sharedData.pixelScale ? m_viewScale.x : 1.f);

        courseEnt.getComponent<cro::Transform>().setPosition(glm::vec3(size / 2.f, -0.1f));
        courseEnt.getComponent<cro::Transform>().setScale(courseScale);
        courseEnt.getComponent<cro::Callback>().active = true; //makes sure to delay so updating the texture size is complete first

        trophyEnt.getComponent<cro::Transform>().setPosition(glm::vec3(size / 2.f, 2.1f));
        trophyEnt.getComponent<cro::Transform>().setScale(courseScale);
        trophyEnt.getComponent<cro::Callback>().active = true;

        //update minimap
        const auto uiSize = size / m_viewScale;
        auto mapSize = glm::vec2(MapSize / 2u);
        mapSize /= 2.f;
        mapEnt.getComponent<cro::Transform>().setPosition({ uiSize.x - mapSize.y, uiSize.y - (mapSize.x) - (UIBarHeight * 1.5f) }); //map sprite is rotated 90


        greenEnt.getComponent<cro::Transform>().setPosition({ 2.f, uiSize.y - (MapSize.y / 2) - UIBarHeight - 2.f });
        greenEnt.getComponent<cro::Transform>().move(glm::vec2(static_cast<float>(MapSize.y) / 4.f));

        windEnt.getComponent<cro::Transform>().setPosition(glm::vec2(/*uiSize.x + */WindIndicatorPosition.x, WindIndicatorPosition.y));

        //update the overlay
        auto colour = cro::Colour(0.f, 0.f, 0.f, 0.25f);
        infoEnt.getComponent<cro::Drawable2D>().getVertexData() =
        {
            //bottom bar
            cro::Vertex2D(glm::vec2(0.f, UIBarHeight), colour),
            cro::Vertex2D(glm::vec2(0.f), colour),
            cro::Vertex2D(glm::vec2(uiSize.x, UIBarHeight), colour),
            cro::Vertex2D(glm::vec2(uiSize.x, 0.f), colour),
            //degen
            cro::Vertex2D(glm::vec2(uiSize.x, 0.f), cro::Colour::Transparent),
            cro::Vertex2D(glm::vec2(0.f, uiSize.y), cro::Colour::Transparent),
            //top bar
            cro::Vertex2D(glm::vec2(0.f, uiSize.y), colour),
            cro::Vertex2D(glm::vec2(0.f, uiSize.y - UIBarHeight), colour),
            cro::Vertex2D(uiSize, colour),
            cro::Vertex2D(glm::vec2(uiSize.x, uiSize.y - UIBarHeight), colour),
        };
        infoEnt.getComponent<cro::Drawable2D>().updateLocalBounds();
        infoEnt.getComponent<cro::Transform>().setScale(m_viewScale);


        //send command to UIElements and reposition
        refreshUI();

        //relocate the power bar
        auto uiPos = glm::vec2(uiSize.x / 2.f, UIBarHeight / 2.f);
        rootNode.getComponent<cro::Transform>().setPosition(uiPos);

        //this calls the update for the scoreboard render texture
        updateScoreboard();
    };

    auto& cam = m_uiScene.getActiveCamera().getComponent<cro::Camera>();
    cam.renderFlags = ~RenderFlags::Reflection;
    cam.resizeCallback = updateView;
    updateView(cam);
    m_uiScene.getActiveCamera().getComponent<cro::Transform>().setPosition({ 0.f, 0.f, 5.f });

    m_emoteWheel.build(infoEnt, m_uiScene, m_resources.textures);
}

void GolfState::createSwingMeter(cro::Entity root)
{
    static constexpr float Width = 4.f;
    static constexpr float Height = 40.f;
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>().setVertexData(
        {
            cro::Vertex2D(glm::vec2(-Width, -Height), SwingputDark),
            cro::Vertex2D(glm::vec2(Width,  -Height), SwingputDark),
            cro::Vertex2D(glm::vec2(-Width,  -0.5f), SwingputDark),
            cro::Vertex2D(glm::vec2(Width,  -0.5f), SwingputDark),

            cro::Vertex2D(glm::vec2(-Width,  -0.5f), TextNormalColour),
            cro::Vertex2D(glm::vec2(Width,  -0.5f), TextNormalColour),
            cro::Vertex2D(glm::vec2(-Width,  0.5f), TextNormalColour),
            cro::Vertex2D(glm::vec2(Width,  0.5f), TextNormalColour),

            cro::Vertex2D(glm::vec2(-Width,  0.5f), SwingputDark),
            cro::Vertex2D(glm::vec2(Width,  0.5f), SwingputDark),
            cro::Vertex2D(glm::vec2(-Width, Height), SwingputDark),
            cro::Vertex2D(glm::vec2(Width,  Height), SwingputDark),


            cro::Vertex2D(glm::vec2(-Width, -Height), SwingputLight),
            cro::Vertex2D(glm::vec2(Width,  -Height), SwingputLight),
            cro::Vertex2D(glm::vec2(-Width, 0.f), SwingputLight),
            cro::Vertex2D(glm::vec2(Width,  0.f), SwingputLight),
        });
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& verts = e.getComponent<cro::Drawable2D>().getVertexData();
        float height = verts[14].position.y;
        float targetAlpha = -0.01f;

        if (m_inputParser.isSwingputActive())
        {
            height = m_inputParser.getSwingputPosition() * ((Height * 2.f) / MaxSwingputDistance);
            targetAlpha = 1.f;
        }

        auto& currentAlpha = e.getComponent<cro::Callback>().getUserData<float>();
        const float InSpeed = dt * 6.f;
        const float OutSpeed = m_inputParser.getPower() < 0.5 ? InSpeed : dt * 0.5f;
        if (currentAlpha <= targetAlpha)
        {
            currentAlpha = std::min(1.f, currentAlpha + InSpeed);
        }
        else
        {
            currentAlpha = std::max(0.f, currentAlpha - OutSpeed);
        }

        for (auto& v : verts)
        {
            v.colour.setAlpha(currentAlpha);
        }
        verts[14].position.y = height;
        verts[15].position.y = height;
    };
    
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;
    entity.addComponent<UIElement>().depth = 0.2f;
    entity.getComponent<UIElement>().relativePosition = { 1.f, 0.f };
    entity.getComponent<UIElement>().absolutePosition = { -10.f, 50.f };
    
    root.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
}

void GolfState::showCountdown(std::uint8_t seconds)
{
    m_roundEnded = true;

    //hide any input
    cro::Command cmd;
    cmd.targetFlags = CommandID::UI::Root;
    cmd.action = [](cro::Entity e, float)
    {
        e.getComponent<cro::Callback>().getUserData<std::pair<std::int32_t, float>>().first = 1;
        e.getComponent<cro::Callback>().active = true;
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //show the scores
    updateScoreboard();
    showScoreboard(true);

    //check if we're the winner
    if (m_statBoardScores.size() > 1) //not the only player
    {
        if (m_statBoardScores[0].client == m_sharedData.clientConnection.connectionID)
        {
            //remember this is auto-disabled if the player is not the only one on the client
            Achievements::awardAchievement(AchievementStrings[AchievementID::LeaderOfThePack]);

            switch (m_sharedData.scoreType)
            {
            default:
            case ScoreType::Stroke:
                Achievements::awardAchievement(AchievementStrings[AchievementID::StrokeOfGenius]);
                break;
            case ScoreType::Match:
                Achievements::awardAchievement(AchievementStrings[AchievementID::NoMatch]);
                break;
            case ScoreType::Skins:
                Achievements::awardAchievement(AchievementStrings[AchievementID::SkinOfYourTeeth]);
                break;
            }

            //message for audio director
            auto* msg = getContext().appInstance.getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
            msg->type = GolfEvent::RoundEnd;
            msg->score = 0;
        }
        else if (m_statBoardScores.back().client == m_sharedData.clientConnection.connectionID)
        {
            auto* msg = getContext().appInstance.getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
            msg->type = GolfEvent::RoundEnd;
            msg->score = 1; //lose
        }
    }

    if(m_holeData.size() > 8) //only consider it a round if there are at least 9 holes
    {
        Achievements::incrementStat(StatStrings[StatID::TotalRounds]);
    }

    if (m_sharedData.holeCount == 0) //set to ALL - which ought to be 18
    {
        Achievements::incrementStat(m_sharedData.mapDirectory);
    }

    auto trophyCount = std::min(std::size_t(3), m_statBoardScores.size());

    for (auto i = 0u; i < trophyCount; ++i)
    {
        if (m_statBoardScores[i].client == m_sharedData.clientConnection.connectionID
            && !m_sharedData.localConnectionData.playerData[m_statBoardScores[i].player].isCPU)
        {
            if (m_statBoardScores.size() > 1
                && m_holeData.size() > 8)
            {
                Achievements::incrementStat(StatStrings[StatID::GoldWins + i]);

                //only award rank XP if there are players to rank against
                //and reduce XP if < 4 players. Probably ought to consider
                //the opponent's XP too, ie award more if a player wins
                //against someone with a significantly higher level.
                float multiplier = std::min(1.f, static_cast<float>(m_statBoardScores.size()) / 4.f);
                float xp = 0.f;
                switch (i)
                {
                default: break;
                case 0:
                    xp = static_cast<float>(XPValues[XPID::First]) * multiplier;
                    break;
                case 1:
                    xp = static_cast<float>(XPValues[XPID::Second]) * multiplier;
                    break;
                case 2:
                    xp = static_cast<float>(XPValues[XPID::Third]) * multiplier;
                    break;
                }
                Social::awardXP(static_cast<std::int32_t>(xp));
            }
        }

        m_trophies[i].getComponent<TrophyDisplay>().state = TrophyDisplay::In;
        //m_trophyLabels[i].getComponent<cro::Callback>().active = true; //this is done by TrophyDisplay (above) to properly delay it
        m_trophyBadges[i].getComponent<cro::SpriteAnimation>().play(std::min(5, m_sharedData.connectionData[m_statBoardScores[i].client].level / 10));
        m_trophyBadges[i].getComponent<cro::Model>().setDoubleSided(0, true);

        m_trophyLabels[i].getComponent<cro::Sprite>().setTexture(m_sharedData.nameTextures[m_statBoardScores[i].client].getTexture(), false);
        auto bounds = m_trophyLabels[i].getComponent<cro::Sprite>().getTextureBounds();
        bounds.bottom = bounds.height * m_statBoardScores[i].player;
        m_trophyLabels[i].getComponent<cro::Sprite>().setTextureRect(bounds);
    }

    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 200.f + scoreboardExpansion, 10.f, 0.23f }); //attaches to scoreboard
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { 200.f, 10.f - UITextPosV };
    entity.getComponent<UIElement>().depth = 0.23f;
    entity.getComponent<UIElement>().resizeCallback = [](cro::Entity e)
    {
        e.getComponent<cro::Transform>().move({ scoreboardExpansion, 0.f }); 
    };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<std::pair<float, std::uint8_t>>(1.f, seconds);
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& [current, sec] = e.getComponent<cro::Callback>().getUserData<std::pair<float, std::uint8_t>>();
        current -= dt;
        if (current < 0)
        {
            current += 1.f;
            sec--;
        }

        if (m_sharedData.tutorial)
        {
            e.getComponent<cro::Text>().setString("Returning to menu in: " + std::to_string(sec));
        }
        else
        {
            e.getComponent<cro::Text>().setString("Returning to lobby in: " + std::to_string(sec));
        }

        auto bounds = cro::Text::getLocalBounds(e);
        bounds.width = std::floor(bounds.width / 2.f);
        e.getComponent<cro::Transform>().setOrigin({ bounds.width, 0.f });
    };

    //attach to the scoreboard
    cmd.targetFlags = CommandID::UI::Scoreboard;
    cmd.action =
        [entity](cro::Entity e, float) mutable
    {
        e.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //create status icons for each connected client
    //to show vote to skip
    auto unreadyRect = m_sprites[SpriteID::QuitNotReady].getTextureRect();
    auto readyRect = m_sprites[SpriteID::QuitReady].getTextureRect();
    const glm::vec2 texSize(m_sprites[SpriteID::QuitNotReady].getTexture()->getSize());
    if (texSize.x != 0 && texSize.y != 0)
    {
        float posOffset = unreadyRect.width;

        unreadyRect.left /= texSize.x;
        unreadyRect.width /= texSize.x;
        unreadyRect.bottom /= texSize.y;
        unreadyRect.height /= texSize.y;

        readyRect.left /= texSize.x;
        readyRect.width /= texSize.x;
        readyRect.bottom /= texSize.y;
        readyRect.height /= texSize.y;

        entity = m_uiScene.createEntity();
        entity.addComponent<cro::Transform>();
        entity.addComponent<cro::Drawable2D>().setTexture(m_sprites[SpriteID::QuitNotReady].getTexture());
        entity.addComponent<cro::Callback>().active = true;
        entity.getComponent<cro::Callback>().function =
            [&, readyRect, unreadyRect, posOffset](cro::Entity e, float)
        {
            auto& tx = e.getComponent<cro::Transform>();
            tx.setPosition({ 13.f, UIBarHeight + 10.f, 2.f });
            tx.setScale(m_viewScale);

            float basePos = 0.f;
            std::vector<cro::Vertex2D> vertices;
            for (auto i = 0u; i < 4u; ++i)
            {
                if (m_sharedData.connectionData[i].playerCount)
                {
                    //check status flags to choose rectangle
                    auto rect = (m_readyQuitFlags & (1 << i)) ? readyRect : unreadyRect;

                    
                    vertices.emplace_back(glm::vec2(basePos, posOffset), glm::vec2(rect.left, rect.bottom + rect.height));
                    vertices.emplace_back(glm::vec2(basePos, 0.f), glm::vec2(rect.left, rect.bottom));
                    vertices.emplace_back(glm::vec2(basePos + posOffset, posOffset), glm::vec2(rect.left + rect.width, rect.bottom + rect.height));
                    vertices.emplace_back(glm::vec2(basePos + posOffset, 0.f), glm::vec2(rect.left + rect.width, rect.bottom));                    

                    vertices.emplace_back(glm::vec2(basePos + posOffset, posOffset), glm::vec2(rect.left + rect.width, rect.bottom + rect.height), cro::Colour::Transparent);
                    vertices.emplace_back(glm::vec2(basePos + posOffset, 0.f), glm::vec2(rect.left + rect.width, rect.bottom), cro::Colour::Transparent);

                    basePos += posOffset + 2.f;

                    vertices.emplace_back(glm::vec2(basePos, posOffset), glm::vec2(rect.left, rect.bottom + rect.height), cro::Colour::Transparent);
                    vertices.emplace_back(glm::vec2(basePos, 0.f), glm::vec2(rect.left, rect.bottom), cro::Colour::Transparent);
                }
            }
            e.getComponent<cro::Drawable2D>().setVertexData(vertices);
        };
    }


    if (cro::Util::Random::value(0, 2) == 0)
    {
        float radius = glm::length(m_holeData[m_currentHole].pin - m_cameras[CameraID::Player].getComponent<cro::Transform>().getWorldPosition()) * 0.85f;

        //add a callback that makes the camera orbit the flag - and hence the drone follows
        m_cameras[CameraID::Sky].getComponent<cro::Callback>().active = true;
        m_cameras[CameraID::Sky].getComponent<cro::Callback>().function =
            [&, radius](cro::Entity e, float dt)
        {
            static float elapsed = 0.f;
            elapsed += dt;

            auto basePos = m_holeData[m_currentHole].pin;
            basePos.x += std::sin(elapsed) * radius;
            basePos.z += std::cos(elapsed) * radius;
            basePos.y += 0.7f;
            e.getComponent<cro::Transform>().setPosition(basePos);
        };
    }


    refreshUI();
}

void GolfState::createScoreboard()
{
    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/scoreboard.spt", m_resources.textures);

    m_sprites[SpriteID::QuitReady] = spriteSheet.getSprite("quit_ready");
    m_sprites[SpriteID::QuitNotReady] = spriteSheet.getSprite("quit_not_ready");

    auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
    size.x /= 2.f;
    size.y -= size.y / 2.f;

    auto rootEnt = m_uiScene.createEntity();
    rootEnt.addComponent<cro::Transform>().setPosition({ size.x, -size.y });
    rootEnt.addComponent<cro::CommandTarget>().ID = CommandID::UI::ScoreboardController;
    //use the callback to keep the board centred/scaled
    rootEnt.addComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        static constexpr float Speed = 10.f;

        auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
        auto target = glm::vec3(size / 2.f, 0.22f);
        target.y -= e.getComponent<cro::Callback>().getUserData<std::int32_t>() * size.y;

        auto pos = e.getComponent<cro::Transform>().getPosition();
        pos += (target - pos) * dt * Speed;

        e.getComponent<cro::Transform>().setPosition(pos);
        e.getComponent<cro::Transform>().setScale(m_viewScale);
    };

    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::Scoreboard | CommandID::UI::UIElement;
    auto bgSprite = spriteSheet.getSprite("border");
    entity.addComponent<UIElement>().absolutePosition = { 0.f, -12.f };
    entity.getComponent<UIElement>().resizeCallback =
        [&,bgSprite](cro::Entity e)
    {
        auto baseSize = glm::vec2(cro::App::getWindow().getSize()) / m_viewScale;
        scoreboardExpansion = stretchToScreen(e, bgSprite, baseSize);
        auto bounds = e.getComponent<cro::Drawable2D>().getLocalBounds();
        e.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    };
    entity.getComponent<UIElement>().resizeCallback(entity);
    entity.getComponent<cro::Drawable2D>().updateLocalBounds();
    auto bounds = entity.getComponent<cro::Drawable2D>().getLocalBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    rootEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    auto bgEnt = entity;

    auto resizeCentre =
        [](cro::Entity e)
    {
        e.getComponent<cro::Transform>().move({ scoreboardExpansion, 0.f });
    };

    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;
    entity.addComponent<UIElement>().absolutePosition = { 200.f, 281.f };
    entity.getComponent<UIElement>().depth = 0.5f;
    entity.getComponent<UIElement>().resizeCallback = resizeCentre;
    entity.addComponent<cro::Text>(font).setString("LEADERS");
    entity.getComponent<cro::Text>().setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    centreText(entity);
    bgEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    if (!m_courseTitle.empty())
    {
        entity = m_uiScene.createEntity();
        entity.addComponent<cro::Transform>();
        entity.addComponent<cro::Drawable2D>();
        entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;
        entity.addComponent<UIElement>().absolutePosition = { 200.f, 11.f };
        entity.getComponent<UIElement>().depth = 0.5f;
        entity.getComponent<UIElement>().resizeCallback = resizeCentre;
        entity.addComponent<cro::Text>(font).setString(m_courseTitle);
        entity.getComponent<cro::Text>().setCharacterSize(UITextSize);
        entity.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
        centreText(entity);
        bgEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    }

    auto connectionCount = 0;
    for (auto i = 1u; i < m_sharedData.connectionData.size(); ++i)
    {
        connectionCount += m_sharedData.connectionData[i].playerCount;
    }
    if (connectionCount != 0)
    {
        auto& smallFont = m_sharedData.sharedResources->fonts.get(FontID::Info);

        entity = m_uiScene.createEntity();
        entity.addComponent<cro::Transform>().setPosition({ 200.f + scoreboardExpansion, 10.f, 0.5f });
        entity.addComponent<cro::Drawable2D>();
        entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::WaitMessage | CommandID::UI::UIElement;
        entity.addComponent<UIElement>().absolutePosition = { 200.f, 10.f - UITextPosV };
        entity.getComponent<UIElement>().relativePosition = { 0.f, 0.f };
        entity.getComponent<UIElement>().depth = 0.5f;
        entity.getComponent<UIElement>().resizeCallback = [](cro::Entity e)
        {
            e.getComponent<cro::Transform>().move({ scoreboardExpansion, 0.f });
        };
        entity.addComponent<cro::Text>(smallFont).setString("Waiting For Other Players");
        entity.getComponent<cro::Text>().setCharacterSize(InfoTextSize);
        entity.getComponent<cro::Text>().setFillColour(TextNormalColour);
        bounds = cro::Text::getLocalBounds(entity);
        bounds.width = std::floor(bounds.width / 2.f);
        entity.getComponent<cro::Transform>().setOrigin({ bounds.width, 0.f });
        bgEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    }

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;
    bgSprite = spriteSheet.getSprite("background");
    entity.addComponent<UIElement>().absolutePosition = { 6.f, -265.f };
    entity.getComponent<UIElement>().depth = 0.2f;
    entity.getComponent<UIElement>().resizeCallback =
        [&, bgSprite](cro::Entity e)
    {
        auto baseSize = glm::vec2(cro::App::getWindow().getSize()) / m_viewScale;
        stretchToScreen(e, bgSprite, baseSize);
        
        //refreshes the cropping area
        cro::Command cmd;
        cmd.targetFlags = CommandID::UI::ScoreScroll;
        cmd.action = [](cro::Entity f, float)
        {
            f.getComponent<cro::Callback>().getUserData<std::int32_t>() = 0;
            f.getComponent<cro::Callback>().active = true;
        };
        m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
    };
    entity.getComponent<UIElement>().resizeCallback(entity);

    bgSprite = spriteSheet.getSprite("board");
    m_leaderboardTexture.init(bgSprite, font);

    cro::FloatRect bgCrop = bgSprite.getTextureBounds();
    bgCrop.bottom += bgCrop.height;

    auto scrollEnt = m_uiScene.createEntity();
    scrollEnt.addComponent<cro::Transform>();
    scrollEnt.addComponent<cro::CommandTarget>().ID = CommandID::UI::ScoreScroll;
    scrollEnt.addComponent<cro::Callback>().setUserData<std::int32_t>(0); //set to the number of steps to scroll
    scrollEnt.getComponent<cro::Callback>().function =
        [bgEnt, entity, bgCrop](cro::Entity e, float) mutable
    {
        auto& steps = e.getComponent<cro::Callback>().getUserData<std::int32_t>();
        static constexpr float StepSize = 14.f;
        static constexpr float MaxMove = StepSize * 19.f;

        auto move = steps * StepSize;
        auto pos = e.getComponent<cro::Transform>().getPosition();
        pos.y = std::min(MaxMove, std::max(0.f, pos.y + move));

        e.getComponent<cro::Transform>().setPosition(pos);
        e.getComponent<cro::Callback>().active = false;
        steps = 0; //this is a reference, don't delete it...

        //update the cropping
        const auto& ents = bgEnt.getComponent<cro::Callback>().getUserData<std::vector<cro::Entity>>();
        for (auto ent : ents)
        {
            auto crop = cro::Text::getLocalBounds(ent);
            crop.width = std::min(crop.width, MinLobbyCropWidth + 16.f + scoreboardExpansion);
            crop.height = bgCrop.height;
            crop.height += 1.f;
            crop.bottom = -(bgCrop.height - 1.f) - pos.y;
            ent.getComponent<cro::Drawable2D>().setCroppingArea(crop);
        }
        //TODO these values need to be rounded to
        //the nearest scaled pixel ie nearest 2,3 or whatever viewScale is
        auto crop = bgCrop;
        crop.width += (scoreboardExpansion * 2.f);
        crop.bottom -= pos.y;
        entity.getComponent<cro::Drawable2D>().setCroppingArea(crop);
    };
    scrollEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    bgEnt.getComponent<cro::Transform>().addChild(scrollEnt.getComponent<cro::Transform>());

    //these have the text components on them, the callback updates scroll cropping
    bgEnt.addComponent<cro::Callback>().setUserData<std::vector<cro::Entity>>();

    //m_scoreColumnCount = 11;
    m_scoreColumnCount = std::min(m_holeData.size() + m_scoreColumnCount, std::size_t(11));

    auto& ents = bgEnt.getComponent<cro::Callback>().getUserData<std::vector<cro::Entity>>();
    ents.resize(m_scoreColumnCount); //title and total
    std::int32_t i = 0;
    for (auto& e : ents)
    {
        e = m_uiScene.createEntity();
        e.addComponent<cro::Transform>().setPosition(glm::vec3(ColumnPositions[i], 1.3f));
        e.addComponent<cro::Drawable2D>();
        e.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
        e.getComponent<cro::Text>().setVerticalSpacing(LeaderboardTextSpacing);
        e.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);

        if (i > 0)
        {
            //UITextPosV gets added by the camera resize callback...
            e.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;
            e.addComponent<UIElement>().absolutePosition = ColumnPositions[i] - glm::vec2(0.f, UITextPosV);
            e.getComponent<UIElement>().depth = 1.3f;
            e.getComponent<UIElement>().resizeCallback =
                [&](cro::Entity ent)
            {
                //gotta admit - I don't know why this works.
                if (scoreboardExpansion > 0)
                {
                    float offset = scoreboardExpansion == MaxExpansion ? std::floor(ColumnMargin) : 0.f;
                    ent.getComponent<cro::Transform>().move({ std::floor(scoreboardExpansion - offset), 0.f});
                }
            };
        }

        scrollEnt.getComponent<cro::Transform>().addChild(e.getComponent<cro::Transform>());
        i++;
    }
    ents.back().getComponent<cro::Transform>().setPosition(glm::vec3(ColumnPositions.back(), 0.5f));
    ents.back().getComponent<UIElement>().absolutePosition = ColumnPositions.back() - glm::vec2(0.f, UITextPosV);

    updateScoreboard();

    //net strength icons
    glm::vec3 iconPos(8.f, 235.f, 2.2f);
    static constexpr float IconSpacing = 14.f;
    for (const auto& c : m_sharedData.connectionData)
    {
        for (auto j = 0u; j < c.playerCount; ++j)
        {
            entity = m_uiScene.createEntity();
            entity.addComponent<cro::Transform>().setPosition(iconPos);
            entity.addComponent<cro::Drawable2D>();
            entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("strength_meter");
            entity.addComponent<cro::SpriteAnimation>();
            entity.addComponent<cro::Callback>().setUserData<std::pair<std::uint8_t, std::uint8_t>>(c.connectionID, j);
            entity.getComponent<cro::Callback>().function =
                [&, bgEnt](cro::Entity e, float)
            {
                auto [client, player] = e.getComponent<cro::Callback>().getUserData<std::pair<std::uint8_t, std::uint8_t>>();

                if (m_sharedData.connectionData[client].playerData[player].isCPU)
                {
                    e.getComponent<cro::SpriteAnimation>().play(5);
                }
                else
                {
                    auto idx = m_sharedData.connectionData[client].pingTime / 30;
                    e.getComponent<cro::SpriteAnimation>().play(std::min(4u, idx));
                }
            };
            bgEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
            m_netStrengthIcons.push_back(entity);

            iconPos.y -= IconSpacing;
        }
    }
}

void GolfState::updateScoreboard()
{
    cro::Command cmd;
    cmd.targetFlags = CommandID::UI::Scoreboard;
    cmd.action = [&](cro::Entity e, float)
    {
        struct ScoreEntry final
        {
            cro::String name;
            std::vector<std::uint8_t> holes;
            std::int32_t frontNine = 0;
            std::int32_t backNine = 0;
            std::int32_t total = 0;
            std::uint8_t client = 0;
            std::uint8_t player = 0;
        };

        std::vector<ScoreEntry> scores;
        m_statBoardScores.clear();

        std::uint32_t playerCount = 0;
        auto holeCount = m_holeData.size();
        std::uint8_t clientID = 0;
        for (const auto& client : m_sharedData.connectionData)
        {
            playerCount += client.playerCount;

            for (auto i = 0u; i < client.playerCount; ++i)
            {
                auto& entry = scores.emplace_back();
                entry.name = client.playerData[i].name;
                entry.client = clientID;
                entry.player = i;

                for (auto j = 0u; j < client.playerData[i].holeScores.size(); ++j)
                {
                    entry.holes.push_back(client.playerData[i].holeScores[j]);
                    if (j < 9)
                    {
                        if (m_sharedData.scoreType == ScoreType::Stroke)
                        {
                            entry.frontNine += client.playerData[i].holeScores[j];
                        }
                        else if (m_sharedData.scoreType == ScoreType::Match)
                        {
                            entry.frontNine = client.playerData[i].matchScore;
                        }
                        else
                        {
                            entry.frontNine = client.playerData[i].skinScore;
                        }
                    }
                    else
                    {
                        if (m_sharedData.scoreType == ScoreType::Stroke)
                        {
                            entry.backNine += client.playerData[i].holeScores[j];
                        }
                        else if (m_sharedData.scoreType == ScoreType::Match)
                        {
                            entry.backNine = client.playerData[i].matchScore;
                        }
                        else
                        {
                            entry.backNine = client.playerData[i].skinScore;
                        }
                    }
                }
                switch (m_sharedData.scoreType)
                {
                default:
                case ScoreType::Stroke:
                    entry.total = entry.frontNine + entry.backNine;
                    break;
                case ScoreType::Skins:
                    entry.total = client.playerData[i].skinScore;
                    break;
                case ScoreType::Match:
                    //entry.total = entry.frontNine;
                    entry.total = client.playerData[i].matchScore;
                    break;
                }

                //for stat/achievment tracking
                auto& leaderboardEntry = m_statBoardScores.emplace_back();
                leaderboardEntry.client = clientID;
                leaderboardEntry.player = i;
                leaderboardEntry.score = entry.total;
            }
            clientID++;
        }

        //tracks stats and decides on trophy layout on round end (see showCountdown())
        std::sort(m_statBoardScores.begin(), m_statBoardScores.end(),
            [&](const StatBoardEntry& a, const StatBoardEntry& b)
            {
                if (m_sharedData.scoreType == ScoreType::Stroke)
                {
                    return a.score < b.score;
                }
                return a.score > b.score;
            });
        //LOG("Table Update", cro::Logger::Type::Info);

        auto& ents = e.getComponent<cro::Callback>().getUserData<std::vector<cro::Entity>>();
        std::sort(scores.begin(), scores.end(),
            [&](const ScoreEntry& a, const ScoreEntry& b)
            {
                switch (m_sharedData.scoreType)
                {
                default:
                case ScoreType::Stroke:
                    return a.total < b.total;
                case ScoreType::Skins:
                case ScoreType::Match:
                    return a.total > b.total;
                }
            });


        std::size_t page2 = 0;
        static constexpr std::size_t MaxCols = 9;
        if (holeCount > m_scoreColumnCount)
        {
            page2 = std::min(MaxCols, holeCount - m_scoreColumnCount);
        }

        //store the strings to update the leaderboard texture
        std::vector<LeaderboardEntry> leaderboardEntries;

        //name column
        cro::String nameString = "HOLE\nPAR";
        for (auto i = 0u; i < playerCount; ++i)
        {
            nameString += "\n  " + scores[i].name.substr(0, ConstVal::MaxStringChars);
            m_netStrengthIcons[i].getComponent<cro::Callback>().getUserData<std::pair<std::uint8_t, std::uint8_t>>()
                = std::make_pair(scores[i].client, scores[i].player);
        }
        if (page2)
        {
            //pad out for page 2
            for (auto i = 0u; i < 16u - playerCount; ++i)
            {
                nameString += "\n";
            }

            nameString += "\n\nHOLE\nPAR";
            for (auto i = 0u; i < playerCount; ++i)
            {
                nameString += "\n  " + scores[i].name.substr(0, ConstVal::MaxStringChars);
            }
        }
        ents[0].getComponent<cro::Text>().setString(nameString);
        leaderboardEntries.emplace_back(ents[0].getComponent<cro::Transform>().getPosition() + glm::vec3(2.f, 0.f, 0.f), nameString);

        //score columns
        for (auto i = 1u; i < ents.size() - 1; ++i)
        {
            std::string scoreString = std::to_string(i) + "\n" + std::to_string(m_holeData[i - 1].par);

            for (auto j = 0u; j < playerCount; ++j)
            {
                scoreString += "\n" + std::to_string(scores[j].holes[i - 1]);
            }

            if (page2)
            {
                for (auto j = 0u; j < 16 - playerCount; ++j)
                {
                    scoreString += "\n";
                }

                auto holeIndex = (i + MaxCols) - 1;
                if (holeIndex < m_holeData.size())
                {
                    scoreString += "\n\n" + std::to_string(i + MaxCols) + "\n" + std::to_string(m_holeData[holeIndex].par);
                    for (auto j = 0u; j < playerCount; ++j)
                    {
                        scoreString += "\n" + std::to_string(scores[j].holes[holeIndex]);
                    }
                }
            }

            ents[i].getComponent<cro::Text>().setString(scoreString);
            leaderboardEntries.emplace_back(glm::vec3(ents[i].getComponent<UIElement>().absolutePosition - glm::vec2(ColumnMargin, -UITextPosV), 0.f), scoreString);
        }

        //total column
        std::int32_t par = 0;
        for (auto i = 0u; i < MaxCols && i < m_holeData.size(); ++i)
        {
            par += m_holeData[i].par;
        }

        std::string totalString = "TOTAL\n" + std::to_string(par);

        for (auto i = 0u; i < playerCount; ++i)
        {
            totalString += "\n" + std::to_string(scores[i].frontNine);

            switch (m_sharedData.scoreType)
            {
            default:
            case ScoreType::Stroke:
                break;
            case ScoreType::Match:
                totalString += " POINTS";
                break;
            case ScoreType::Skins:
                totalString += " SKINS";
                break;
            }
        }

        //pad out for page 2
        for (auto i = 0u; i < 16u - playerCount; ++i)
        {
            totalString += "\n";
        }

        if (page2)
        {
            const auto getSeparator = 
                [](std::int32_t first)
            {
                std::string str;
                if (first < 10)
                {
                    str += " ";
                }
                str += " - ";

                return str;
            };

            auto frontPar = par;
            par = 0;
            for (auto i = MaxCols; i < m_holeData.size(); ++i)
            {
                par += m_holeData[i].par;
            }
            auto separator = getSeparator(par);

            totalString += "\n\nTOTAL\n" + std::to_string(par) + separator + std::to_string(par + frontPar);
            for (auto i = 0u; i < playerCount; ++i)
            {
                separator = getSeparator(scores[i].backNine);
                totalString += "\n" + std::to_string(scores[i].backNine);
                
                switch (m_sharedData.scoreType)
                {
                default:
                case ScoreType::Stroke:
                    totalString += separator + std::to_string(scores[i].total);
                    break;
                case ScoreType::Match:
                    totalString += " POINTS";
                    break;
                case ScoreType::Skins:
                    totalString += " SKINS";
                    break;
                }
            }
        }

        ents.back().getComponent<cro::Text>().setString(totalString);
        ents.back().getComponent<cro::Transform>().setPosition(ColumnPositions.back());
        //gotta admit - I don't know why this works.
        if (scoreboardExpansion > 0)
        {
            float offset = scoreboardExpansion == MaxExpansion ? std::floor(ColumnMargin) : 0.f;
            ents.back().getComponent<cro::Transform>().move({ std::floor(scoreboardExpansion - offset), 0.f });
        }
        leaderboardEntries.emplace_back(glm::vec3(ents.back().getComponent<UIElement>().absolutePosition - glm::vec2(ColumnMargin, -UITextPosV), 0.f), totalString);

        //for some reason we have to hack this to display and I'm too lazy to debug it
        auto pos = ents.back().getComponent<cro::Transform>().getPosition();
        pos.z = 1.5f;
        ents.back().getComponent<cro::Transform>().setPosition(pos);

        m_leaderboardTexture.update(leaderboardEntries);
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
}

void GolfState::showScoreboard(bool visible)
{
    for (auto e : m_netStrengthIcons)
    {
        e.getComponent<cro::Callback>().active = visible;
    }

    if (m_currentPlayer.client == m_sharedData.clientConnection.connectionID)
    {
        if (m_inputParser.inProgress())
        {
            return;
        }

        //disable the input while the score is shown
        m_inputParser.setSuspended(visible);
    }

    //don't hide if the round finished
    if (m_roundEnded)
    {
        visible = true;
    }

    auto target = visible ? 0 : 1; //when 1 board is moved 1x screen size from centre

    cro::Command cmd;
    cmd.targetFlags = CommandID::UI::ScoreboardController;
    cmd.action = [target](cro::Entity e, float)
    {
        e.getComponent<cro::Callback>().active = true;
        e.getComponent<cro::Callback>().setUserData<std::int32_t>(target);
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);


    std::int32_t step = -19;
    if (m_currentHole > 8)
    {
        //scroll to lower part of the board
        step = 19;
    }

    cmd.targetFlags = CommandID::UI::ScoreScroll;
    cmd.action = [step](cro::Entity e, float)
    {
        e.getComponent<cro::Callback>().getUserData<std::int32_t>() = step;
        e.getComponent<cro::Callback>().active = true;
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    if (!visible)
    {
        //hide message
        cmd.targetFlags = CommandID::UI::WaitMessage;
        cmd.action =
            [&](cro::Entity e, float)
        {
            e.getComponent<cro::Transform>().setScale({ 0.f, 1.f });
        };
        m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
    }
}

void GolfState::updateWindDisplay(glm::vec3 direction)
{
    float rotation = std::atan2(-direction.z, direction.x);
    m_windUpdate.windVector = direction;

    cro::Command cmd;
    cmd.targetFlags = CommandID::UI::WindSock;
    cmd.action = [&, rotation](cro::Entity e, float dt)
    {
        auto camRotation = m_camRotation;
        if (m_currentCamera != CameraID::Player)
        {
            //set the rotation relative to the active cam
            auto vec = m_cameras[m_currentCamera].getComponent<cro::Transform>().getForwardVector();
            camRotation = std::atan2(-vec.z, vec.x);
        }

        auto r = rotation - camRotation;

        float& currRotation = e.getComponent<float>();
        currRotation += cro::Util::Maths::shortestRotation(currRotation, r) * (dt * 4.f);
        e.getComponent<cro::Transform>().setRotation(currRotation);
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    cmd.targetFlags = CommandID::UI::WindString;
    cmd.action = [direction](cro::Entity e, float dt)
    {
        float knots = direction.y * KnotsPerMetre;
        std::stringstream ss;
        ss.precision(2);
        ss << std::fixed << knots << " knots";
        e.getComponent<cro::Text>().setString(ss.str());

        auto bounds = cro::Text::getLocalBounds(e);
        bounds.width = std::floor(bounds.width / 2.f);
        e.getComponent<cro::Transform>().setOrigin({ bounds.width, 0.f });

        if (knots < 1.5f)
        {
            if (knots < 1)
            {
                e.getComponent<cro::Text>().setFillColour(TextNormalColour);
            }
            else
            {
                e.getComponent<cro::Text>().setFillColour(TextGoldColour);
            }
        }
        else
        {
            e.getComponent<cro::Text>().setFillColour(TextOrangeColour);
        }
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    cmd.targetFlags = CommandID::Flag;
    cmd.action = [rotation](cro::Entity e, float dt)
    {
        float& currRotation = e.getComponent<float>();
        currRotation += cro::Util::Maths::shortestRotation(currRotation, rotation) * dt;
        e.getComponent<cro::Transform>().setRotation(cro::Transform::Y_AXIS, currRotation);
    };
    m_gameScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    cmd.targetFlags = CommandID::UI::WindSpeed;
    cmd.action = [direction](cro::Entity e, float)
    {
        e.getComponent<cro::Callback>().setUserData<float>(-direction.y);
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
}

void GolfState::showMessageBoard(MessageBoardID messageType, bool special)
{
    auto bounds = m_sprites[SpriteID::MessageBoard].getTextureBounds();
    auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
    auto position = glm::vec3(size.x / 2.f, (size.y / 2.f) + UIBarHeight * m_viewScale.y, 0.05f);

    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(position);
    entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, 0.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::MessageBoard];
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::MessageBoard;


    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

    auto textEnt = m_uiScene.createEntity();
    textEnt.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 56.f, 0.02f });
    textEnt.addComponent<cro::Drawable2D>();
    textEnt.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    textEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);

    auto textEnt2 = m_uiScene.createEntity();
    textEnt2.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 26.f, 0.02f });
    textEnt2.addComponent<cro::Drawable2D>();
    textEnt2.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    textEnt2.getComponent<cro::Text>().setFillColour(TextNormalColour);

    auto textEnt3 = m_uiScene.createEntity();
    textEnt3.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 41.f, 0.02f });
    textEnt3.addComponent<cro::Drawable2D>();
    textEnt3.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    textEnt3.getComponent<cro::Text>().setFillColour(TextNormalColour);

    //add mini graphic depending on message type
    auto imgEnt = m_uiScene.createEntity();
    imgEnt.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, bounds.height / 2.f, 0.01f });
    imgEnt.getComponent<cro::Transform>().move(glm::vec2(0.f, -6.f));
    imgEnt.addComponent<cro::Drawable2D>();

    switch (messageType)
    {
    default: break;
    case MessageBoardID::HoleScore:
    case MessageBoardID::Gimme:
    {
        std::int32_t score = m_sharedData.connectionData[m_currentPlayer.client].playerData[m_currentPlayer.player].holeScores[m_currentHole];
        auto overPar = score - m_holeData[m_currentHole].par;
      
        //if this is a remote player the score won't
        //have arrived yet, so kludge this here so the
        //display type is correct.
        if (m_currentPlayer.client != m_sharedData.clientConnection.connectionID)
        {
            score++;
        }

        if (score > 1)
        {
            score -= m_holeData[m_currentHole].par;
            score += ScoreID::ScoreOffset;
        }
        else
        {
            //hio is also technically an eagle or birdie
            //etc, so we need to differentiate
            score = ScoreID::HIO;
            Social::awardXP(XPValues[XPID::HIO]);
        }


        //if this is a local player update achievements
        if (m_currentPlayer.client == m_sharedData.clientConnection.connectionID)
        {
            if (m_hadFoul && overPar < 1)
            {
                Achievements::awardAchievement(AchievementStrings[AchievementID::Boomerang]);
            }

            switch (score)
            {
            default: break;
            case ScoreID::Birdie:
                Achievements::incrementStat(StatStrings[StatID::Birdies]);
                break;
            case ScoreID::Eagle:
                Achievements::incrementStat(StatStrings[StatID::Eagles]);
                Achievements::awardAchievement(AchievementStrings[AchievementID::Soaring]);
                break;
            case ScoreID::HIO:
                Achievements::incrementStat(StatStrings[StatID::HIOs]);
                Achievements::awardAchievement(AchievementStrings[AchievementID::HoleInOne]);
                break;
            }
        }

        if (messageType == MessageBoardID::HoleScore)
        {
            //this triggers the VO which we only want if it went in the hole.
            auto* msg = cro::App::getInstance().getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
            msg->type = GolfEvent::Scored;
            msg->score = static_cast<std::uint8_t>(score);
            msg->travelDistance = glm::length2(m_holeData[m_currentHole].pin - m_currentPlayer.position);
            msg->club = getClub();
        }


        if (score < ScoreID::Count)
        {
            textEnt.getComponent<cro::Text>().setString(ScoreStrings[score]);
            textEnt.getComponent<cro::Transform>().move({ 0.f, -10.f, 0.f });

            std::int32_t divisor = m_sharedData.showPuttingPower ? 2 : 1;

            switch (score)
            {
            default: break;
            case ScoreID::Albatross:
                Social::awardXP(XPValues[XPID::Albatross] / divisor);
                break;
            case ScoreID::Eagle:
                Social::awardXP(XPValues[XPID::Eagle] / divisor);
                break;
            case ScoreID::Birdie:
                Social::awardXP(XPValues[XPID::Birdie] / divisor);
                break;
            case ScoreID::Par:
                Social::awardXP(XPValues[XPID::Par] / divisor);
                break;
            }

            if (special)
            {
                textEnt3.getComponent<cro::Text>().setString("Nice Putt!");
                textEnt3.getComponent<cro::Text>().setFillColour(TextGoldColour);
                textEnt3.getComponent<cro::Transform>().move({ 0.f, -8.f });

                textEnt.getComponent<cro::Transform>().move({ 0.f, 2.f, 0.f });
            }
        }
        else
        {
            textEnt.getComponent<cro::Text>().setString("Bad Luck!");
            textEnt2.getComponent<cro::Text>().setString(std::to_string(overPar) + " Over Par");
        }
    }
    break;
    case MessageBoardID::Bunker:
        textEnt.getComponent<cro::Text>().setString("Bunker!");
        imgEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::Bunker];
        bounds = m_sprites[SpriteID::Bunker].getTextureBounds();
        break;
    case MessageBoardID::PlayerName:
        textEnt.getComponent<cro::Text>().setString(m_sharedData.connectionData[m_currentPlayer.client].playerData[m_currentPlayer.player].name.substr(0, 19));
        textEnt.getComponent<cro::Text>().setFillColour(TextGoldColour);
        textEnt2.getComponent<cro::Text>().setString("Stroke: " + std::to_string(m_sharedData.connectionData[m_currentPlayer.client].playerData[m_currentPlayer.player].holeScores[m_currentHole] + 1));
        textEnt3.getComponent<cro::Text>().setString(ScoreTypes[m_sharedData.scoreType]);
        break;
    case MessageBoardID::Scrub:
    case MessageBoardID::Water:
        textEnt.getComponent<cro::Text>().setString("Foul!");
        textEnt.getComponent<cro::Text>().setFillColour(TextGoldColour);
        textEnt2.getComponent<cro::Text>().setString("1 Stroke Penalty");
        imgEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::Foul];
        bounds = m_sprites[SpriteID::Foul].getTextureBounds();
        break;
    }
    imgEnt.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f, 0.f });

    centreText(textEnt);
    centreText(textEnt2);
    centreText(textEnt3);
    
    entity.getComponent<cro::Transform>().addChild(textEnt.getComponent<cro::Transform>());
    entity.getComponent<cro::Transform>().addChild(textEnt2.getComponent<cro::Transform>());
    entity.getComponent<cro::Transform>().addChild(textEnt3.getComponent<cro::Transform>());
    entity.getComponent<cro::Transform>().addChild(imgEnt.getComponent<cro::Transform>());

    //callback for anim/self destruction
    struct MessageAnim final
    {
        enum
        {
            Delay, Open, Hold, Close
        }state = Delay;
        float currentTime = 0.5f;
    };
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<MessageAnim>();
    entity.getComponent<cro::Callback>().function =
        [&, textEnt, textEnt2, textEnt3, imgEnt, messageType](cro::Entity e, float dt)
    {
        static constexpr float HoldTime = 2.f;
        auto& [state, currTime] = e.getComponent<cro::Callback>().getUserData<MessageAnim>();
        switch (state)
        {
        default: break;
        case MessageAnim::Delay:
            currTime = std::max(0.f, currTime - dt);
            if (currTime == 0)
            {
                state = MessageAnim::Open;
            }
            break;
        case MessageAnim::Open:
            //grow
            currTime = std::min(1.f, currTime + (dt * 2.f));
            e.getComponent<cro::Transform>().setScale(glm::vec2(m_viewScale.x, m_viewScale.y * cro::Util::Easing::easeOutQuint(currTime)));
            if (currTime == 1)
            {
                currTime = 0;
                state = MessageAnim::Hold;
            }
            break;
        case MessageAnim::Hold:
            //hold
            currTime = std::min(HoldTime, currTime + dt);
            if (currTime == HoldTime)
            {
                currTime = 1.f;
                state = MessageAnim::Close;
            }
            break;
        case MessageAnim::Close:
            //shrink
            currTime = std::max(0.f, currTime - (dt * 3.f));
            e.getComponent<cro::Transform>().setScale(glm::vec2(m_viewScale.x * cro::Util::Easing::easeInCubic(currTime), m_viewScale.y));
            if (currTime == 0)
            {
                e.getComponent<cro::Callback>().active = false;
                m_uiScene.destroyEntity(textEnt);
                m_uiScene.destroyEntity(textEnt2);
                m_uiScene.destroyEntity(textEnt3);
                m_uiScene.destroyEntity(imgEnt);
                m_uiScene.destroyEntity(e);

                if (messageType == MessageBoardID::PlayerName)
                {
                    //this assumes it was raised by an event
                    //from requestNextPlayer
                    setCurrentPlayer(m_currentPlayer);
                }
            }
            break;
        }
    };


    //send a message to immediately close any current open messages
    cro::Command cmd;
    cmd.targetFlags = CommandID::UI::MessageBoard;
    cmd.action = [entity](cro::Entity e, float)
    {
        if (e != entity)
        {
            auto& [state, currTime] = e.getComponent<cro::Callback>().getUserData<MessageAnim>();
            if (state != MessageAnim::Close)
            {
                currTime = 1.f;
                state = MessageAnim::Close;
            }
        }
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
}

void GolfState::floatingMessage(const std::string& msg)
{
    auto& font = m_sharedData.sharedResources->fonts.get(FontID::Info);

    glm::vec2 size = glm::vec2(GolfGame::getActiveTarget()->getSize());
    glm::vec3 position((size.x / 2.f), (UIBarHeight + 14.f) * m_viewScale.y, 0.2f);

    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(position);
    entity.getComponent<cro::Transform>().setScale(m_viewScale);
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(font).setString(msg);
    entity.getComponent<cro::Text>().setFillColour(TextNormalColour);
    entity.getComponent<cro::Text>().setCharacterSize(InfoTextSize);
    centreText(entity);

    entity.addComponent<FloatingText>().basePos = position;
}

void GolfState::createTransition()
{
    glm::vec2 screenSize(cro::App::getWindow().getSize());
    auto& shader = m_resources.shaders.get(ShaderID::Transition);

    auto entity = m_uiScene.createEntity();
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
        static constexpr float MaxTime = 2.f - (1.f/60.f);
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

void GolfState::notifyAchievement(const std::array<std::uint8_t, 2u>& data)
{
    //only notify if someone else
    if (m_sharedData.localConnectionData.connectionID != data[0])
    {
        //this came off the network so better validate it a bit...
        if (data[0] < 4
            && m_sharedData.connectionData[data[0]].playerCount != 0
            && data[1] < AchievementID::Count - 1)
        {
            auto name = m_sharedData.connectionData[data[0]].playerData[0].name;
            auto achievement = AchievementLabels[data[1]];

            showNotification(name + " achieved " + achievement);

            auto* msg = postMessage<Social::SocialEvent>(Social::MessageID::SocialMessage);
            msg->type = Social::SocialEvent::PlayerAchievement;
            msg->level = 0; //cheer
        }
    }
}

void GolfState::showNotification(const cro::String& msg)
{
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 4.f * m_viewScale.x, UIBarHeight * m_viewScale.y * 2.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(m_sharedData.sharedResources->fonts.get(FontID::UI));
    entity.getComponent<cro::Text>().setCharacterSize(8u * static_cast<std::uint32_t>(m_viewScale.y));
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    entity.addComponent<Notification>().message = msg;
}

void GolfState::showLevelUp(std::uint64_t levelData)
{
    std::int32_t level =   (levelData & 0x00000000FFFFFFFF);
    std::int32_t player = ((levelData & 0x000000FF00000000) >> 32);
    std::int32_t client = ((levelData & 0x0000FF0000000000) >> 40);

    m_sharedData.connectionData[client].level = std::uint8_t(level);

    cro::String msg = m_sharedData.connectionData[client].playerData[player].name;
    msg += " has reached level " + std::to_string(level);
    showNotification(msg);
}

void GolfState::toggleQuitReady()
{
    if (m_roundEnded)
    {
        m_sharedData.clientConnection.netClient.sendPacket<std::uint8_t>(PacketID::ReadyQuit, m_sharedData.clientConnection.connectionID, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
    }
}

void GolfState::refreshUI()
{
    auto uiSize = glm::vec2(GolfGame::getActiveTarget()->getSize()) / m_viewScale;

    cro::Command cmd;
    cmd.targetFlags = CommandID::UI::UIElement;
    cmd.action = [&, uiSize](cro::Entity e, float)
    {
        auto pos = e.getComponent<UIElement>().relativePosition;
        pos.x *= uiSize.x;
        pos.x = std::floor(pos.x);
        pos.y *= (uiSize.y - UIBarHeight);
        pos.y = std::round(pos.y);
        pos.y += UITextPosV;

        pos += e.getComponent<UIElement>().absolutePosition;

        e.getComponent<cro::Transform>().setPosition(glm::vec3(pos, e.getComponent<UIElement>().depth));

        if (e.getComponent<UIElement>().resizeCallback)
        {
            e.getComponent<UIElement>().resizeCallback(e);
        }
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
}

void GolfState::buildTrophyScene()
{
    auto updateCam = [&](cro::Camera& cam)
    {
        auto vpSize = calcVPSize();

        auto winSize = glm::vec2(480.f, 360.f);
        float maxScale = std::floor(static_cast<float>(cro::App::getWindow().getSize().y) / vpSize.y);
        if (!m_sharedData.pixelScale)
        {
            winSize *= maxScale;
        }

        std::uint32_t samples = m_sharedData.pixelScale ? 0 :
            m_sharedData.antialias ? m_sharedData.multisamples : 0;

        m_trophySceneTexture.create(static_cast<std::uint32_t>(winSize.x), static_cast<std::uint32_t>(winSize.y), true, false, samples);
        
        float ratio = winSize.x / winSize.y;
        cam.setPerspective(m_sharedData.fov * cro::Util::Const::degToRad, ratio, 0.1f, 10.f);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };
    };

    auto& cam = m_trophyScene.getActiveCamera().getComponent<cro::Camera>();
    cam.resizeCallback = updateCam;
    cam.isStatic = true;
    updateCam(cam);

    auto sunEnt = m_trophyScene.getSunlight();
    sunEnt.getComponent<cro::Transform>().setRotation(cro::Transform::Y_AXIS, -15.f * cro::Util::Const::degToRad);
    sunEnt.getComponent<cro::Transform>().setRotation(cro::Transform::X_AXIS, -10.f * cro::Util::Const::degToRad);

    const std::array<std::pair<std::string, glm::vec3>, 3u> Paths =
    {
        std::make_pair("assets/golf/models/trophies/trophy01.cmt", glm::vec3(0.f, -0.3f, -1.f)),
        std::make_pair("assets/golf/models/trophies/trophy02.cmt", glm::vec3(-0.55f, -0.315f, -1.f)),
        std::make_pair("assets/golf/models/trophies/trophy03.cmt", glm::vec3(0.55f, -0.33f, -1.f))        
    };

    cro::EmitterSettings emitterSettings;
    emitterSettings.loadFromFile("assets/golf/particles/firework.cps", m_resources.textures);

    if (emitterSettings.releaseCount == 0)
    {
        emitterSettings.releaseCount = 10;
    }

    cro::AudioScape as;
    as.loadFromFile("assets/golf/sound/menu.xas", m_resources.audio);

    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/player_menu.spt", m_resources.textures);

    std::int32_t i = 0;
    cro::ModelDefinition md(m_resources);
    for (const auto& [path, position] : Paths)
    {
        if (md.loadFromFile(path))
        {
            auto entity = m_trophyScene.createEntity();
            entity.addComponent<cro::Transform>().setPosition(position);
            entity.getComponent<cro::Transform>().setScale(glm::vec3(0.f));
            md.createModel(entity);

            //TODO there's no gaurantee that the materials are in this order...
            auto material = m_resources.materials.get(m_materialIDs[MaterialID::Trophy]);
            applyMaterialData(md, material);
            entity.getComponent<cro::Model>().setMaterial(0, material);

            material = m_resources.materials.get(m_materialIDs[MaterialID::Ball]); //doesn't pixel fade like Cel does.
            applyMaterialData(md, material);
            entity.getComponent<cro::Model>().setMaterial(1, material);

            entity.addComponent<TrophyDisplay>().delay = static_cast<float>(i) / 2.f;
            entity.addComponent<cro::ParticleEmitter>().settings = emitterSettings;

            entity.addComponent<cro::AudioEmitter>() = as.getEmitter("firework");
            entity.getComponent<cro::AudioEmitter>().setPitch(static_cast<float>(cro::Util::Random::value(8, 11)) / 10.f);
            entity.getComponent<cro::AudioEmitter>().setLooped(false);

            m_trophies[i] = entity;
            auto trophyEnt = entity;

            //badge
            entity = m_trophyScene.createEntity();
            entity.addComponent<cro::Transform>().setPosition({ 0.f, 0.08f, 0.06f });
            entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("rank_icon");
            auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
            bounds.width /= m_trophyScene.getSystem<cro::SpriteSystem3D>()->getPixelsPerUnit();
            entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, 0.f });
            entity.addComponent<cro::Model>();
            entity.addComponent<cro::SpriteAnimation>();
            trophyEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
            m_trophyBadges[i] = entity;

            //name label
            entity = m_uiScene.createEntity();
            entity.addComponent<cro::Transform>().setScale(glm::vec2(0.f));
            entity.addComponent<cro::Drawable2D>();
            entity.addComponent<cro::Sprite>(m_sharedData.nameTextures[0].getTexture());
            bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
            bounds.height -= LabelIconSize.y;
            bounds.height /= 4.f;
            bounds.bottom = bounds.height * i;
            entity.getComponent<cro::Sprite>().setTextureRect(bounds);
            entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f, -0.2f });
            auto p = position;
            entity.addComponent<cro::Callback>().function =
                [&,p,trophyEnt](cro::Entity e, float)
            {
                //the trophy scene sprite is set as user data in createUI()
                auto b = e.getComponent<cro::Callback>().getUserData<cro::Entity>().getComponent<cro::Sprite>().getTextureBounds();
                glm::vec2 pos(std::floor(((b.width / 1.5f) * p.x) + (b.width / 2.f)), std::floor(((b.height / 2.f) * p.y) + (b.height / 3.f)));

                float parentScale = e.getComponent<cro::Callback>().getUserData<cro::Entity>().getComponent<cro::Transform>().getScale().x;

                e.getComponent<cro::Transform>().setPosition(pos);
                e.getComponent<cro::Transform>().setScale(trophyEnt.getComponent<cro::Transform>().getScale() * (m_viewScale.x / parentScale));
            };
            trophyEnt.getComponent<TrophyDisplay>().label = entity;

            m_trophyLabels[i] = entity;


            //icon if available
            if (Social::isAvailable())
            {
                entity = m_uiScene.createEntity();
                entity.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, bounds.height, 0.1f });
                entity.addComponent<cro::Drawable2D>();
                entity.addComponent<cro::Sprite>(m_sharedData.nameTextures[0].getTexture());
                bounds = { 0.f, LabelTextureSize.y - LabelIconSize.y, LabelIconSize.x, LabelIconSize.y };
                entity.getComponent<cro::Sprite>().setTextureRect(bounds);
                entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, -14.f, -0.1f });
                m_trophyLabels[i].getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

                entity.addComponent<cro::Callback>().active = true;
                entity.getComponent<cro::Callback>().setUserData<float>(1.f);
                entity.getComponent<cro::Callback>().function =
                    [&,i](cro::Entity e, float dt)
                {
                    if (m_trophyLabels[i].getComponent<cro::Callback>().active)
                    {
                        e.getComponent<cro::Sprite>().setTexture(*m_trophyLabels[i].getComponent<cro::Sprite>().getTexture(), false);

                        static constexpr float BaseScale = 0.25f;
                        static constexpr float SpinCount = 6.f;
                        static constexpr float Duration = 3.f;

                        auto& currTime = e.getComponent<cro::Callback>().getUserData<float>();
                        currTime = std::max(0.f, currTime - (dt / Duration));

                        float progress = cro::Util::Easing::easeInQuart(currTime) * SpinCount;
                        float scale = std::cos(cro::Util::Const::TAU * progress);

                        scale += 1.f;
                        scale /= 2.f;
                        scale *= BaseScale;

                        e.getComponent<cro::Transform>().setScale({ scale, BaseScale });

                        if (currTime == 0)
                        {
                            e.getComponent<cro::Callback>().active = false;
                            e.getComponent<cro::Transform>().setScale({ BaseScale, BaseScale });
                        }
                    }
                };
            }
        }
        ++i;
    }

    //we have to make sure everything is processed at least once
    //as a hacky way of making sure the double sided property is
    //applied to the badge materials.
    m_trophyScene.simulate(0.f);
}

void GolfState::updateMiniMap()
{
    cro::Command cmd;
    cmd.targetFlags = CommandID::UI::MiniMap;
    cmd.action = [&](cro::Entity en, float)
    {
        //trigger animation - this does the actual render
        en.getComponent<cro::Callback>().active = true;
        m_mapCam.getComponent<cro::Camera>().active = true;
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
}

glm::vec2 GolfState::toMinimapCoords(glm::vec3 worldPos) const
{
    auto origin = m_holeData[m_currentHole].modelEntity.getComponent<cro::Transform>().getOrigin();
    worldPos -= m_minimapOffset;
    worldPos -= origin;
    worldPos *= m_minimapScale;
    worldPos += origin;
    worldPos /= 2.f;

    glm::vec2 result = { worldPos.x, -worldPos.z };

    if (m_minimapRotation)
    {
        static constexpr glm::vec2 MapCentre(MapSize / 4u);
        result -= MapCentre;

        //assume we're only ever rotatating 90 deg
        if (m_minimapRotation < 0)
        {
            result = { -result.y, result.x };
        }
        else
        {
            result = { result.y, -result.x };
        }
        result += MapCentre;
    }

    return result;
}

//------emote wheel-----//
void GolfState::EmoteWheel::build(cro::Entity root, cro::Scene& uiScene, cro::TextureResource& textures)
{
    if (sharedData.tutorial)
    {
        //don't need this.
        return;
    }

    auto entity = uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.5f };
    entity.getComponent<UIElement>().depth = 0.5f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement;

    root.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    rootNode = entity;


    cro::SpriteSheet spriteSheet;
    if (spriteSheet.loadFromFile("assets/golf/sprites/emotes.spt", textures))
    {
        const std::array SpriteNames =
        {
            std::string("happy_large"),
            std::string("grumpy_large"),
            std::string("laughing_large"),
            std::string("sad_large")
        };

        auto& font = sharedData.sharedResources->fonts.get(FontID::UI);

        struct AnimData final
        {
            enum
            {
                In, Out
            }state = In;
            float progress = 1.f;
        };

        for (auto i = 0u; i < EmotePositions.size(); ++i)
        {
            entity = uiScene.createEntity();
            entity.addComponent<cro::Transform>().setPosition(EmotePositions[i]);
            entity.addComponent<cro::Drawable2D>();
            entity.addComponent<cro::Sprite>() = spriteSheet.getSprite(SpriteNames[i]);
            auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
            entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });

            entity.addComponent<cro::Callback>().setUserData<AnimData>();
            entity.getComponent<cro::Callback>().function =
                [&](cro::Entity e, float dt)
            {
                const float Speed = dt * 10.f;
                auto& data = e.getComponent<cro::Callback>().getUserData<AnimData>();
                if (data.state == AnimData::In)
                {
                    data.progress = std::max(0.f, data.progress - Speed);
                    if (data.progress == 0)
                    {
                        data.state = AnimData::Out;
                    }
                }
                else
                {
                    data.progress = std::min(1.f, data.progress + Speed);
                    if (data.progress == 1)
                    {
                        data.state = AnimData::In;
                        targetScale = 0.f;
                        e.getComponent<cro::Callback>().active = false;
                    }
                }

                cro::Colour c(1.f, data.progress, data.progress);
                e.getComponent<cro::Sprite>().setColour(c);

                float scale = cro::Util::Easing::easeOutCirc(data.progress);
                e.getComponent<cro::Transform>().setScale({ scale, 1.f });
            };

            rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
            buttonNodes[i] = entity;

            auto labelEnt = uiScene.createEntity();
            labelEnt.addComponent<cro::Transform>().setPosition(entity.getComponent<cro::Transform>().getOrigin() + glm::vec3(0.f, 20.f, 0.f));
            labelEnt.addComponent<cro::Drawable2D>();
            labelEnt.addComponent<cro::Text>(font);
            labelEnt.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
            /*labelEnt.getComponent<cro::Text>().setShadowColour(LeaderboardTextDark);
            labelEnt.getComponent<cro::Text>().setShadowOffset({ 1.f,-1.f });*/
            labelEnt.getComponent<cro::Text>().setCharacterSize(UITextSize);
            entity.getComponent<cro::Transform>().addChild(labelEnt.getComponent<cro::Transform>());
            labelNodes[i] = labelEnt;
        }
        refreshLabels();

        entity = uiScene.createEntity();
        entity.addComponent<cro::Transform>().setPosition(glm::vec3(0.f, 0.f, 0.13f));
        entity.addComponent<cro::Drawable2D>();
        entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("background");
        auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
        entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });

        rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    }
}

bool GolfState::EmoteWheel::handleEvent(const cro::Event& evt)
{
    if (sharedData.tutorial)
    {
        return false;
    }

    const auto sendEmote = [&](std::uint8_t emoteID, std::int32_t controllerID)
    {
        std::uint32_t data = 0;
        if (currentPlayer.client == sharedData.localConnectionData.connectionID)
        {
            data |= (sharedData.localConnectionData.connectionID << 16) | (currentPlayer.player << 8) | (emoteID);
        }
        else
        {
            data |= (sharedData.localConnectionData.connectionID << 16) | (std::uint8_t(controllerID) << 8) | (emoteID);
        }
        sharedData.clientConnection.netClient.sendPacket(PacketID::Emote, data, net::NetFlag::Reliable, ConstVal::NetChannelReliable);

        cooldown = 6.f;
        buttonNodes[emoteID].getComponent<cro::Callback>().active = true; //play anim which also closes wheel
    };

    if (cooldown > 0)
    {
        return false;
    }

    if (evt.type == SDL_KEYDOWN
        && evt.key.repeat == 0)
    {
        switch (evt.key.keysym.sym)
        {
        default: break;
        case SDLK_LCTRL:
            targetScale = 1.f;
            return true;
        }

        //stop these getting forwarded to input parser
        if (evt.key.keysym.mod & KMOD_LCTRL)
        {
            if (evt.key.keysym.sym == sharedData.inputBinding.keys[InputBinding::Up])
            {
                return true;
            }
            else if (evt.key.keysym.sym == sharedData.inputBinding.keys[InputBinding::Down])
            {
                return true;
            }
            else if (evt.key.keysym.sym == sharedData.inputBinding.keys[InputBinding::Left])
            {
                return true;
            }
            else if (evt.key.keysym.sym == sharedData.inputBinding.keys[InputBinding::Right])
            {
                return true;
            }
        }
    }
    else if (evt.type == SDL_KEYUP)
    {
        switch (evt.key.keysym.sym)
        {
        default: break;
        case SDLK_LCTRL:
            targetScale = 0.f;
            return true;
        }

        if (currentScale == 1)
        {
            if (evt.key.keysym.sym == sharedData.inputBinding.keys[InputBinding::Up])
            {
                sendEmote(Emote::Happy, 0);
                return true;
            }
            else if (evt.key.keysym.sym == sharedData.inputBinding.keys[InputBinding::Down])
            {
                sendEmote(Emote::Laughing, 0);
                return true;
            }
            else if (evt.key.keysym.sym == sharedData.inputBinding.keys[InputBinding::Left])
            {
                sendEmote(Emote::Sad, 0);
                return true;
            }
            else if (evt.key.keysym.sym == sharedData.inputBinding.keys[InputBinding::Right])
            {
                sendEmote(Emote::Grumpy, 0);
                return true;
            }
        }
    }


    else if (evt.type == SDL_CONTROLLERBUTTONDOWN)
    {
        auto controllerID = activeControllerID(sharedData.inputBinding.playerID);
        if (cro::GameController::controllerID(evt.cbutton.which) == controllerID)
        {
            switch (evt.cbutton.button)
            {
            default: break;
            case cro::GameController::ButtonY:
                targetScale = 1.f;
                return true;
            }
        }

        //prevent these getting forwarded to input parser if wheel is open
        if (cro::GameController::isButtonPressed(controllerID, cro::GameController::ButtonY))
        {
            switch (evt.cbutton.button)
            {
            default: return false;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                return true;
            }
        }
    }
    else if (evt.type == SDL_CONTROLLERBUTTONUP)
    {
        auto controllerID = activeControllerID(sharedData.inputBinding.playerID);

        if (cro::GameController::controllerID(evt.cbutton.which) == controllerID)
        {
            switch (evt.cbutton.button)
            {
            default: break;
            case cro::GameController::ButtonY:
                targetScale = 0.f;
                return true;
            }
        }

        if (currentScale == 1)
        {
            switch (evt.cbutton.button)
            {
            default: return false;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                sendEmote(Emote::Happy, controllerID);
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                sendEmote(Emote::Laughing, controllerID);
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                sendEmote(Emote::Sad, controllerID);
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                sendEmote(Emote::Grumpy, controllerID);
                return true;
            }
        }
    }

    return false;
}

void GolfState::EmoteWheel::update(float dt)
{
    if (sharedData.tutorial)
    {
        return;
    }

    const float speed = dt * 10.f;
    if (currentScale < targetScale)
    {
        currentScale = std::min(targetScale, currentScale + speed);
    }
    else if (currentScale > targetScale)
    {
        currentScale = std::max(targetScale, currentScale - speed);
    }

    float scale = cro::Util::Easing::easeOutCirc(currentScale);
    rootNode.getComponent<cro::Transform>().setScale(glm::vec2(scale));

    cooldown -= dt;
}

void GolfState::EmoteWheel::refreshLabels()
{
    if (sharedData.tutorial)
    {
        //these won't exist
        return;
    }

    const std::array InputMap =
    {
        InputBinding::Up,
        InputBinding::Right,
        InputBinding::Down,
        InputBinding::Left,
    };

    for (auto i = 0u; i < labelNodes.size(); ++i)
    {
        labelNodes[i].getComponent<cro::Text>().setString(SDL_GetKeyName(sharedData.inputBinding.keys[InputMap[i]]));
        centreText(labelNodes[i]);

        if (cro::GameController::getControllerCount() == 0)
        {
            labelNodes[i].getComponent<cro::Transform>().setScale({ 1.f, 1.f });
        }
        else
        {
            labelNodes[i].getComponent<cro::Transform>().setScale({ 1.f, 0.f });
        }
    }
}

void GolfState::showEmote(std::uint32_t data)
{
    std::uint8_t client = (data & 0x00ff0000) >> 16;
    std::uint8_t player = (data & 0x0000ff00) >> 8;
    std::uint8_t emote = (data & 0x000000ff);

    client = std::min(client, std::uint8_t(3u));
    player = std::min(player, std::uint8_t(3u));

    auto msg = m_sharedData.connectionData[client].playerData[player].name;
    msg += " is ";

    std::int32_t emoteID = SpriteID::EmoteHappy;
    switch (emote)
    {
    default:
        msg += "undecided";
        break;
    case Emote::Happy:
        msg += "happy";
        break;
    case Emote::Grumpy:
        msg += "grumpy";
        emoteID = SpriteID::EmoteGrumpy;
        break;
    case Emote::Laughing:
        msg += "laughing";
        emoteID = SpriteID::EmoteLaugh;
        break;
    case Emote::Sad:
        msg += "sad";
        emoteID = SpriteID::EmoteSad;
        break;
    }

    showNotification(msg);


    struct EmoteData final
    {
        float velocity = 50.f;
        float decayRate = cro::Util::Random::value(13.f, 15.5f);
        float rotation = cro::Util::Random::value(-1.f, 1.f);
    };

    glm::vec3 pos(32.f, -16.f, 0.2f);
    for (auto i = 0u; i < 5u; ++i)
    {
        auto ent = m_uiScene.createEntity();
        ent.addComponent<cro::Transform>().setPosition(pos);
        ent.addComponent<cro::Drawable2D>();
        ent.addComponent<cro::Sprite>() = m_sprites[emoteID];
        ent.addComponent<cro::Callback>().active = true;
        ent.getComponent<cro::Callback>().setUserData<EmoteData>();
        ent.getComponent<cro::Callback>().function =
            [&](cro::Entity e, float dt)
        {
            auto& data = e.getComponent<cro::Callback>().getUserData<EmoteData>();
            data.velocity = std::max(0.f, data.velocity - (dt * data.decayRate/* * m_viewScale.y*/));
            data.rotation *= 0.999f;

            if (data.velocity == 0)
            {
                e.getComponent<cro::Callback>().active = false;
                m_uiScene.destroyEntity(e);
            }
            e.getComponent<cro::Transform>().setScale(m_viewScale);
            e.getComponent<cro::Transform>().move({ 0.f, data.velocity * m_viewScale.y * dt, 0.f });
            e.getComponent<cro::Transform>().rotate(data.rotation * dt);
        };

        auto bounds = ent.getComponent<cro::Sprite>().getTextureBounds();
        ent.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });

        pos.x += static_cast<float>(cro::Util::Random::value(24, 38)) * m_viewScale.x;
        pos.y = -static_cast<float>(cro::Util::Random::value(1, 3)) * 10.f;
    }
}