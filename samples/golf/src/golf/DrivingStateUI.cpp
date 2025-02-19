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
#include "GameConsts.hpp"
#include "MenuConsts.hpp"
#include "CommandIDs.hpp"
#include "MessageIDs.hpp"
#include "TextAnimCallback.hpp"
#include "DrivingRangeDirector.hpp"
#include "CloudSystem.hpp"
#include "BallSystem.hpp"
#include "FloatingTextSystem.hpp"

#include <Achievements.hpp>
#include <AchievementStrings.hpp>

#include <crogine/audio/AudioScape.hpp>

#include <crogine/ecs/components/Transform.hpp>
#include <crogine/ecs/components/Drawable2D.hpp>
#include <crogine/ecs/components/Callback.hpp>
#include <crogine/ecs/components/CommandTarget.hpp>
#include <crogine/ecs/components/Camera.hpp>
#include <crogine/ecs/components/UIInput.hpp>
#include <crogine/ecs/components/SpriteAnimation.hpp>
#include <crogine/ecs/components/ParticleEmitter.hpp>
#include <crogine/ecs/components/AudioEmitter.hpp>

#include <crogine/graphics/SpriteSheet.hpp>
#include <crogine/util/Maths.hpp>
#include <crogine/util/Random.hpp>

namespace
{
    //used as indices when scrolling through leaderboards
    std::int32_t leaderboardID = 0;
    std::int32_t leaderboardFilter = 0;
    constexpr std::int32_t MaxLeaderboardFilter = 3;

    static constexpr float SummaryOffset = 54.f;
    static constexpr float SummaryHeight = 254.f;

    static constexpr float BadScore = 50.f;
    static constexpr float GoodScore = 75.f;
    static constexpr float ExcellentScore = 95.f;

    //callback data for anim/self destruction
    //of messages / options window
    struct MessageAnim final
    {
        enum
        {
            Delay, Open, Hold, Close
        }state = Delay;
        float currentTime = 0.5f;
    };

    struct MenuCallback final
    {
        const glm::vec2& viewScale;
        cro::UISystem* uiSystem = nullptr;
        std::int32_t menuID = DrivingState::MenuID::Dummy;

        MenuCallback(const glm::vec2& v, cro::UISystem* ui, std::int32_t id)
            : viewScale(v), uiSystem(ui), menuID(id) {}

        void operator ()(cro::Entity e, float dt)
        {
            auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
            auto position = glm::vec3(size.x / 2.f, size.y / 2.f, 1.5f);

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
                e.getComponent<cro::Transform>().setPosition(position);
                e.getComponent<cro::Transform>().setScale(glm::vec2(viewScale.x, viewScale.y * cro::Util::Easing::easeOutQuint(currTime)));
                if (currTime == 1)
                {
                    currTime = 0;
                    state = MessageAnim::Hold;

                    //set UI active
                    uiSystem->setActiveGroup(menuID);
                }
                break;
            case MessageAnim::Hold:
            {
                //hold - make sure we stay centred
                e.getComponent<cro::Transform>().setPosition(position);
                e.getComponent<cro::Transform>().setScale(viewScale);
            }
            break;
            case MessageAnim::Close:
                //shrink
                currTime = std::max(0.f, currTime - (dt * 3.f));
                e.getComponent<cro::Transform>().setScale(glm::vec2(viewScale.x * cro::Util::Easing::easeInCubic(currTime), viewScale.y));
                if (currTime == 0)
                {
                    e.getComponent<cro::Callback>().active = false;
                    e.getComponent<cro::Transform>().setPosition({ -10000.f, -10000.f });

                    state = MessageAnim::Delay;
                    currTime = 0.75f;
                }
                break;
            }
        }
    };

    struct StarAnimCallback final
    {
        float delay = 0.f;
        float currentTime = 0.f;

        explicit StarAnimCallback(float d) : delay(d), currentTime(d) {}

        void operator() (cro::Entity e, float dt)
        {
            currentTime = std::max(0.f, currentTime - dt);

            if (currentTime == 0)
            {
                currentTime = delay;
                e.getComponent<cro::SpriteAnimation>().play(1);
                e.getComponent<cro::Callback>().active = false;
                e.getComponent<cro::ParticleEmitter>().start();
                e.getComponent<cro::AudioEmitter>().play();
            }
        }
    };
}

void DrivingState::createUI()
{
    //displays the game scene
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>(m_backgroundTexture.getTexture());
    auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec2(bounds.width / 2.f, bounds.height / 2.f));
    entity.addComponent<cro::Callback>().function =
        [](cro::Entity e, float)
    {
        //this is activated once to make sure the
        //sprite is up to date with any texture buffer resize
        glm::vec2 texSize = e.getComponent<cro::Sprite>().getTexture()->getSize();
        e.getComponent<cro::Sprite>().setTextureRect({ glm::vec2(0.f), texSize });
        e.getComponent<cro::Transform>().setOrigin(texSize / 2.f);
        e.getComponent<cro::Callback>().active = false;
    };
    auto courseEnt = entity;
    createPlayer(courseEnt);
    createBall(); //hmmm should probably be in createScene()?

    //info panel background - vertices are set in resize callback
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    auto infoEnt = entity;
    createSwingMeter(entity);

    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

    //player's name
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::PlayerName | CommandID::UI::UIElement;
    entity.addComponent<UIElement>().relativePosition = { 0.2f, 0.f };
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    entity.addComponent<cro::Callback>().setUserData<TextCallbackData>();
    entity.getComponent<cro::Callback>().function = TextAnimCallback();
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //hole distance
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::PinDistance | CommandID::UI::UIElement;
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 1.f };
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
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //current turn
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::UIElement | CommandID::UI::HoleNumber;
    entity.addComponent<UIElement>().relativePosition = { 0.75f, 1.f };
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(LeaderboardTextLight);
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
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec3(bounds.width / 2.f, bounds.height / 2.f, 0.01f));
    entity.getComponent<cro::Transform>().setPosition(windDial.getComponent<cro::Transform>().getOrigin());
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function =
        [](cro::Entity e, float)
    {
        auto speed = e.getComponent<cro::Callback>().getUserData<float>();
        e.getComponent<cro::Transform>().rotate(speed / 6.f);
    };
    windDial.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //ui is attached to this for relative scaling
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(UIHiddenPosition);
    entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::Root;
    entity.addComponent<cro::Callback>().setUserData<std::pair<std::int32_t, float>>(0, 0.f);
    entity.getComponent<cro::Callback>().function =
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

    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    auto rootNode = entity;

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PowerBar];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec2(bounds.width / 2.f, bounds.height / 2.f));
    rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //power bar
    auto barEnt = entity;
    auto barCentre = bounds.width / 2.f;
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(glm::vec2(5.f, 0.f)); //TODO expel the magic number!!
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
    entity.addComponent<cro::Transform>().setPosition(glm::vec3(barCentre, 8.f, 0.1f)); //TODO expel the magic number!!
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::HookBar];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin(glm::vec2(bounds.width / 2.f, bounds.height / 2.f));
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, barCentre](cro::Entity e, float)
    {
        glm::vec3 pos(barCentre + (barCentre * m_inputParser.getHook()), 8.f, 0.1f);
        e.getComponent<cro::Transform>().setPosition(pos);
    };
    barEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());


    //camera for mini map
    auto updateMiniView = [&](cro::Camera& miniCam) mutable
    {
        glm::uvec2 previewSize(RangeSize / 2.f);
        m_mapTexture.create(previewSize.x, previewSize.y);

        miniCam.setOrthographic((-RangeSize.x / 2.f) + 1.f, (RangeSize.x / 2.f) - 1.f, -RangeSize.y / 2.f, RangeSize.y / 2.f, -0.1f, 7.f);
        float xPixel = 1.f / (RangeSize.x / 2.f);
        float yPixel = 1.f / (RangeSize.y / 2.f);
        miniCam.viewport = { xPixel, yPixel, 1.f - (xPixel * 2.f), 1.f - (yPixel * 2.f) };
    };

    m_mapCam = m_gameScene.createEntity();
    m_mapCam.addComponent<cro::Transform>().setPosition({ 0.f, 5.f, 0.f });
    m_mapCam.getComponent<cro::Transform>().rotate(cro::Transform::X_AXIS, -90.f * cro::Util::Const::degToRad);
    auto& miniCam = m_mapCam.addComponent<cro::Camera>();
    miniCam.renderFlags = RenderFlags::MiniMap;
    //miniCam.resizeCallback = updateMiniView;
    updateMiniView(miniCam);

    //minimap view
    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/scoreboard.spt", m_resources.textures);
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, 82.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("minimap");
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    infoEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    auto mapEnt = entity;

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, std::ceil(bounds.height / 2.f) + 1.f, 0.2f });
    entity.getComponent<cro::Transform>().setScale({ 0.f, 0.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>(m_mapTexture.getTexture());
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::MiniMap;
    entity.addComponent<cro::Callback>().setUserData<std::pair<std::int32_t, float>>(0, 1.f);
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& [state, scale] = e.getComponent<cro::Callback>().getUserData<std::pair<std::int32_t, float>>();
        float speed = dt * 4.f;
        float newScale = 0.f;

        if (state == 0)
        {
            //shrinking
            scale = std::max(0.f, scale - speed);
            newScale = cro::Util::Easing::easeOutSine(scale);

            if (scale == 0)
            {
                //update render
                updateMinimap();
                e.getComponent<cro::Sprite>().setTexture(m_mapTexture.getTexture());
                auto bounds = e.getComponent<cro::Sprite>().getTextureBounds();
                e.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });

                //and set to grow
                state = 1;
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
        e.getComponent<cro::Transform>().setScale(glm::vec2(newScale, 1.f));
    };
    mapEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    auto miniEnt = entity;

    mapEnt.addComponent<cro::Callback>().active = true;
    mapEnt.getComponent<cro::Callback>().function =
        [miniEnt](cro::Entity e, float)
    {
        e.getComponent<cro::Transform>().setScale(miniEnt.getComponent<cro::Transform>().getScale());
    };

    //ball icon on mini map
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(PlayerPosition); //actually hides ball off map until ready to be drawn
    entity.addComponent<cro::Drawable2D>().getVertexData() =
    {
        cro::Vertex2D(glm::vec2(-0.5f, 0.5f), TextNormalColour),
        cro::Vertex2D(glm::vec2(-0.5f), TextNormalColour),
        cro::Vertex2D(glm::vec2(0.5f), TextNormalColour),
        cro::Vertex2D(glm::vec2(0.5f, -0.5f), TextNormalColour)
    };
    entity.getComponent<cro::Drawable2D>().updateLocalBounds();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::MiniBall;
    entity.addComponent<cro::Callback>().setUserData<float>(1.f);
    entity.getComponent<cro::Callback>().function =
        [](cro::Entity e, float dt)
    {
        auto& currTime = e.getComponent<cro::Callback>().getUserData<float>();
        currTime = std::max(0.f, currTime - (dt * 3.f));

        static constexpr float MaxScale = 6.f - 1.f;
        float scale = 1.f + (MaxScale * currTime);
        e.getComponent<cro::Transform>().setScale(glm::vec2(scale));

        float alpha = 1.f - currTime;
        auto& verts = e.getComponent<cro::Drawable2D>().getVertexData();
        for (auto& v : verts)
        {
            v.colour.setAlpha(alpha);
        }

        if (currTime == 0)
        {
            currTime = 1.f;
            e.getComponent<cro::Callback>().active = false;
        }
    };
    miniEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());


    //stroke indicator
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ PlayerPosition.x / 2.f, -PlayerPosition.z / 2.f, 0.01f });
    entity.getComponent<cro::Transform>().move(RangeSize / 4.f);
    auto endColour = TextGoldColour;
    endColour.setAlpha(0.f);
    entity.addComponent<cro::Drawable2D>().getVertexData() =
    {
        cro::Vertex2D(glm::vec2(-0.5f, 18.f), endColour),
        cro::Vertex2D(glm::vec2(-0.5f), TextGoldColour),
        cro::Vertex2D(glm::vec2(0.5f, 18.f), endColour),
        cro::Vertex2D(glm::vec2(0.5f, -0.5f), TextGoldColour)
    };
    entity.getComponent<cro::Drawable2D>().updateLocalBounds();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float)
    {
        e.getComponent<cro::Transform>().setRotation(m_inputParser.getYaw());
    };
    miniEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());


    //ui viewport is set 1:1 with window, then the scene
    //is scaled to best-fit to maintain pixel accuracy of text.
    auto updateView = [&, rootNode, courseEnt, infoEnt, windEnt, mapEnt](cro::Camera& cam) mutable
    {
        auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
        cam.setOrthographic(0.f, size.x, 0.f, size.y, -2.5f, 2.f);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };

        auto vpSize = calcVPSize();
        m_viewScale = glm::vec2(std::floor(size.y / vpSize.y));
        m_inputParser.setMouseScale(m_viewScale.x);

        glm::vec2 courseScale(m_sharedData.pixelScale ? m_viewScale.x : 1.f);

        courseEnt.getComponent<cro::Transform>().setPosition(glm::vec3(size / 2.f, -0.1f));
        courseEnt.getComponent<cro::Transform>().setScale(courseScale);
        courseEnt.getComponent<cro::Callback>().active = true; //makes sure to delay so updating the texture size is complete first

        //ui layout
        const auto uiSize = size / m_viewScale;

        auto mapSize = RangeSize / 4.f;
        mapEnt.getComponent<cro::Transform>().setPosition({ uiSize.x - mapSize.x - UIBarHeight, uiSize.y - (mapSize.y) - (UIBarHeight * 1.5f) });

        windEnt.getComponent<cro::Transform>().setPosition(glm::vec2(/*uiSize.x +*/ WindIndicatorPosition.x, WindIndicatorPosition.y - UIBarHeight));

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
        cro::Command cmd;
        cmd.targetFlags = CommandID::UI::UIElement;
        cmd.action = [&, uiSize](cro::Entity e, float)
        {
            auto pos = e.getComponent<UIElement>().relativePosition;
            pos.x *= uiSize.x;
            pos.x = std::round(pos.x);
            pos.y *= (uiSize.y - UIBarHeight);
            pos.y = std::round(pos.y);
            pos.y += UITextPosV;

            pos += e.getComponent<UIElement>().absolutePosition;

            e.getComponent<cro::Transform>().setPosition(glm::vec3(pos, e.getComponent<UIElement>().depth));
        };
        m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

        //relocate the power bar
        auto uiPos = glm::vec2(uiSize.x / 2.f, UIBarHeight / 2.f);
        rootNode.getComponent<cro::Transform>().setPosition(uiPos);
    };

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Camera>().resizeCallback = updateView;
    m_uiScene.setActiveCamera(entity);
    updateView(entity.getComponent<cro::Camera>());

    createGameOptions();
    createSummary();
}

void DrivingState::createSwingMeter(cro::Entity root)
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


void DrivingState::createGameOptions()
{
    const auto centreSprite = [](cro::Entity e)
    {
        auto bounds = e.getComponent<cro::Sprite>().getTextureBounds();
        e.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    };

    auto* uiSystem = m_uiScene.getSystem<cro::UISystem>();
    auto buttonSelect = uiSystem->addCallback([](cro::Entity e)
        {
            e.getComponent<cro::Sprite>().setColour(cro::Colour::White); 
            e.getComponent<cro::AudioEmitter>().play();
        });
    auto buttonUnselect = uiSystem->addCallback([](cro::Entity e) { e.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent); });


    //consumes events when menu not active
    auto dummyEnt = m_uiScene.createEntity();
    dummyEnt.addComponent<cro::Transform>();
    dummyEnt.addComponent<cro::UIInput>();

    cro::AudioScape as;
    as.loadFromFile("assets/golf/sound/menu.xas", m_resources.audio);


    //background
    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/scoreboard.spt", m_resources.textures);
    m_sprites[SpriteID::Stars] = spriteSheet.getSprite("orbs");
    auto bgSprite = spriteSheet.getSprite("border");

    auto bounds = bgSprite.getTextureBounds();
    auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
    auto position = glm::vec3(size.x / 2.f, size.y / 2.f, 1.5f);

    auto bgEntity = m_uiScene.createEntity();
    bgEntity.addComponent<cro::Transform>().setPosition(position);
    bgEntity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
    bgEntity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    bgEntity.addComponent<cro::Drawable2D>();
    bgEntity.addComponent<cro::Sprite>() = bgSprite;
    bgEntity.addComponent<cro::CommandTarget>().ID = CommandID::UI::DrivingBoard;
    bgEntity.addComponent<cro::Callback>().setUserData<MessageAnim>();
    bgEntity.getComponent<cro::Callback>().function = MenuCallback(m_viewScale, uiSystem, MenuID::Options);
    
    auto& smallFont = m_sharedData.sharedResources->fonts.get(FontID::Info);
    auto& largeFont = m_sharedData.sharedResources->fonts.get(FontID::UI);

    //title
    auto titleText = m_uiScene.createEntity();
    titleText.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 293.f, 0.02f });
    titleText.addComponent<cro::Drawable2D>();
    titleText.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    titleText.getComponent<cro::Text>().setFillColour(TextNormalColour);
    titleText.getComponent<cro::Text>().setString("The Range");
    centreText(titleText);
    bgEntity.getComponent<cro::Transform>().addChild(titleText.getComponent<cro::Transform>());

    //header
    auto headerText = m_uiScene.createEntity();
    headerText.addComponent<cro::Transform>().setPosition({ 25.f, 248.f, 0.02f });
    headerText.addComponent<cro::Drawable2D>();
    headerText.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    headerText.getComponent<cro::Text>().setFillColour(TextNormalColour);
    headerText.getComponent<cro::Text>().setString("How To Play");
    bgEntity.getComponent<cro::Transform>().addChild(headerText.getComponent<cro::Transform>());

    //help text
    auto infoText = m_uiScene.createEntity();
    infoText.addComponent<cro::Transform>().setPosition({ 25.f, 237.f, 0.02f });
    infoText.addComponent<cro::Drawable2D>();
    infoText.addComponent<cro::Text>(smallFont).setCharacterSize(InfoTextSize);
    infoText.getComponent<cro::Text>().setFillColour(TextNormalColour);
    const std::string helpString =
        R"(
Pick the number of strokes you wish to take. Hit the ball as close
as possible to the target by selecting the appropriate club. When 
all of your strokes are taken you will be given a score based on 
your overall accuracy. Good Luck!
    )";

    infoText.getComponent<cro::Text>().setString(helpString);
    bgEntity.getComponent<cro::Transform>().addChild(infoText.getComponent<cro::Transform>());


    const auto createButton = [&](const std::string& sprite, glm::vec2 position)
    {
        auto buttonEnt = m_uiScene.createEntity();
        buttonEnt.addComponent<cro::Transform>().setPosition(glm::vec3(position, 0.4f));
        buttonEnt.addComponent<cro::Drawable2D>();
        buttonEnt.addComponent<cro::Sprite>() = spriteSheet.getSprite(sprite);
        buttonEnt.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        buttonEnt.addComponent<cro::UIInput>().area = buttonEnt.getComponent<cro::Sprite>().getTextureBounds();
        buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = buttonSelect;
        buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = buttonUnselect;
        buttonEnt.getComponent<cro::UIInput>().setGroup(MenuID::Options);
        buttonEnt.addComponent<cro::AudioEmitter>() = as.getEmitter("switch");

        return buttonEnt;
    };


    //hole count
    auto countEnt = m_uiScene.createEntity();
    countEnt.addComponent<cro::Transform>().setPosition({ bounds.width / 5.f, 74.f, 0.1f });
    countEnt.addComponent<cro::Drawable2D>();
    countEnt.addComponent<cro::Sprite>() = spriteSheet.getSprite("stroke_select");
    auto strokeBounds = spriteSheet.getSprite("stroke_select").getTextureBounds();
    countEnt.getComponent<cro::Transform>().setOrigin({ strokeBounds.width / 2.f, 0.f });
    bgEntity.getComponent<cro::Transform>().addChild(countEnt.getComponent<cro::Transform>());

    auto strokeTextEnt = m_uiScene.createEntity();
    strokeTextEnt.addComponent<cro::Transform>().setPosition({ strokeBounds.width / 2.f, strokeBounds.height + 22.f });
    strokeTextEnt.addComponent<cro::Drawable2D>();
    strokeTextEnt.addComponent<cro::Text>(largeFont).setString("Strokes\nTo Play");
    strokeTextEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    strokeTextEnt.getComponent<cro::Text>().setCharacterSize(UITextSize);
    strokeTextEnt.getComponent<cro::Text>().setVerticalSpacing(2.f);
    centreText(strokeTextEnt);
    countEnt.getComponent<cro::Transform>().addChild(strokeTextEnt.getComponent<cro::Transform>());

    auto numberEnt = m_uiScene.createEntity();
    numberEnt.addComponent<cro::Transform>().setPosition({ strokeBounds.width / 2.f, std::floor(strokeBounds.height / 2.f) + 4.f, 0.02f });
    numberEnt.addComponent<cro::Drawable2D>();
    numberEnt.addComponent<cro::Text>(largeFont);
    numberEnt.getComponent<cro::Text>().setString("5");
    numberEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    numberEnt.getComponent<cro::Text>().setCharacterSize(UITextSize);
    centreText(numberEnt);
    countEnt.getComponent<cro::Transform>().addChild(numberEnt.getComponent<cro::Transform>());


    //high score text
    auto textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ bounds.width / 5.f, 63.f, 0.02f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Text>(smallFont).setCharacterSize(InfoTextSize);
    textEnt4.getComponent<cro::Text>().setFillColour(TextNormalColour);
    textEnt4.addComponent<cro::Callback>().active = true;
    textEnt4.getComponent<cro::Callback>().function = //make sure to update with current high score
        [&, bgEntity](cro::Entity e, float)
    {
        const auto& data = bgEntity.getComponent<cro::Callback>().getUserData<MessageAnim>();
        if (data.state == MessageAnim::Open)
        {
            if (m_topScores[m_strokeCountIndex] > 0)
            {
                std::stringstream s;
                s.precision(3);
                s << "Personal Best: " << m_topScores[m_strokeCountIndex] << "%";

                e.getComponent<cro::Text>().setString(s.str());
            }
            else
            {
                e.getComponent<cro::Text>().setString("No Score");
            }
            centreText(e);
        }
    };
    bgEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());



    //hole count buttons
    auto buttonEnt = createButton("arrow_left", glm::vec2(-3.f, 3.f));
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, numberEnt, textEnt4](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    m_strokeCountIndex = (m_strokeCountIndex + (m_strokeCounts.size() - 1)) % m_strokeCounts.size();
                    numberEnt.getComponent<cro::Text>().setString(std::to_string(m_strokeCounts[m_strokeCountIndex]));
                    centreText(numberEnt);

                    if (m_topScores[m_strokeCountIndex] > 0)
                    {
                        std::stringstream s;
                        s.precision(3);
                        s << "Personal Best: " << m_topScores[m_strokeCountIndex] << "%";

                        textEnt4.getComponent<cro::Text>().setString(s.str());
                    }
                    else
                    {
                        textEnt4.getComponent<cro::Text>().setString("No Score");
                    }
                    centreText(textEnt4);

                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                }
            });
    countEnt.getComponent<cro::Transform>().addChild(buttonEnt.getComponent<cro::Transform>());

    buttonEnt = createButton("arrow_right", glm::vec2(35.f, 3.f));
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, numberEnt, textEnt4](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    m_strokeCountIndex = (m_strokeCountIndex + 1) % m_strokeCounts.size();
                    numberEnt.getComponent<cro::Text>().setString(std::to_string(m_strokeCounts[m_strokeCountIndex]));
                    centreText(numberEnt);

                    if (m_topScores[m_strokeCountIndex] > 0)
                    {
                        std::stringstream s;
                        s.precision(3);
                        s << "Personal Best: " << m_topScores[m_strokeCountIndex] << "%";

                        textEnt4.getComponent<cro::Text>().setString(s.str());
                    }
                    else
                    {
                        textEnt4.getComponent<cro::Text>().setString("No Score");
                    }
                    centreText(textEnt4);

                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                }
            });
    countEnt.getComponent<cro::Transform>().addChild(buttonEnt.getComponent<cro::Transform>());



    //minimap for targets (delay before rendering)
    auto renderEnt = m_uiScene.createEntity();
    renderEnt.addComponent<cro::Callback>().active = true;
    renderEnt.getComponent<cro::Callback>().setUserData<float>(1.f);
    renderEnt.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& currTime = e.getComponent<cro::Callback>().getUserData<float>();
        currTime -= dt;

        if (currTime < 0)
        {
            auto oldCam = m_gameScene.setActiveCamera(m_mapCam);
            m_mapTexture.clear(TextNormalColour);
            m_gameScene.render();
            m_mapTexture.display();
            m_gameScene.setActiveCamera(oldCam);

            e.getComponent<cro::Callback>().active = false;
            m_uiScene.destroyEntity(e);
        }
    };

    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 110.f, 0.3f });
    entity.getComponent<cro::Transform>().setOrigin(RangeSize / 4.f);
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>(m_mapTexture.getTexture());
    bgEntity.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    auto mapEnt = entity;
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(mapEnt.getComponent<cro::Transform>().getOrigin());
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("minimap");
    auto border = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ border.width / 2.f, border.height / 2.f, 0.1f });
    mapEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    cro::SpriteSheet flagSheet;
    flagSheet.loadFromFile("assets/golf/sprites/ui.spt", m_resources.textures);
    auto flagEnt = m_uiScene.createEntity();
    flagEnt.addComponent<cro::Transform>().setOrigin({0.f, 0.f, -0.1f});
    flagEnt.addComponent<cro::Drawable2D>();
    flagEnt.addComponent<cro::Sprite>() = flagSheet.getSprite("flag03");

    flagEnt.addComponent<cro::Callback>().active = !m_holeData.empty();
    flagEnt.getComponent<cro::Callback>().setUserData<float>(0.f);
    flagEnt.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        static constexpr glm::vec2 Offset(RangeSize / 4.f);
        glm::vec2 flagPos(0.f);

        if (m_targetIndex == 0)
        {
            auto& currTime = e.getComponent<cro::Callback>().getUserData<float>();
            currTime -= dt;

            if (currTime < 0.f)
            {
                currTime += 0.5f;

                static std::size_t idx = 0;
                idx = (idx + cro::Util::Random::value(1, 3)) % m_holeData.size();

                auto pos = m_holeData[idx].pin / 2.f;
                flagPos = { pos.x, -pos.z };

                flagPos += Offset;
                e.getComponent<cro::Transform>().setPosition(flagPos);
            }
        }
        else
        {
            auto pos = m_holeData[m_targetIndex - 1].pin / 2.f;
            flagPos = { pos.x, -pos.z };

            flagPos += Offset;
            e.getComponent<cro::Transform>().setPosition(flagPos);
        }
    };

    mapEnt.getComponent<cro::Transform>().addChild(flagEnt.getComponent<cro::Transform>());


    //target select
    countEnt = m_uiScene.createEntity();
    countEnt.addComponent<cro::Transform>().setPosition({ bounds.width - (bounds.width / 5.f), 74.f, 0.1f });
    countEnt.addComponent<cro::Drawable2D>();
    countEnt.addComponent<cro::Sprite>() = spriteSheet.getSprite("stroke_select");
    strokeBounds = spriteSheet.getSprite("stroke_select").getTextureBounds();
    countEnt.getComponent<cro::Transform>().setOrigin({ strokeBounds.width / 2.f, 0.f });
    bgEntity.getComponent<cro::Transform>().addChild(countEnt.getComponent<cro::Transform>());

    strokeTextEnt = m_uiScene.createEntity();
    strokeTextEnt.addComponent<cro::Transform>().setPosition({ strokeBounds.width / 2.f, strokeBounds.height + 22.f });
    strokeTextEnt.addComponent<cro::Drawable2D>();
    strokeTextEnt.addComponent<cro::Text>(largeFont).setString("Select\nTarget");
    strokeTextEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    strokeTextEnt.getComponent<cro::Text>().setCharacterSize(UITextSize);
    strokeTextEnt.getComponent<cro::Text>().setVerticalSpacing(2.f);
    centreText(strokeTextEnt);
    countEnt.getComponent<cro::Transform>().addChild(strokeTextEnt.getComponent<cro::Transform>());

    numberEnt = m_uiScene.createEntity();
    numberEnt.addComponent<cro::Transform>().setPosition({ strokeBounds.width / 2.f, std::floor(strokeBounds.height / 2.f) + 4.f, 0.02f });
    numberEnt.addComponent<cro::Drawable2D>();
    numberEnt.addComponent<cro::Text>(largeFont);
    numberEnt.getComponent<cro::Text>().setString("?");
    numberEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    numberEnt.getComponent<cro::Text>().setCharacterSize(UITextSize);
    centreText(numberEnt);
    countEnt.getComponent<cro::Transform>().addChild(numberEnt.getComponent<cro::Transform>());


    //target select buttons
    buttonEnt = createButton("arrow_left", glm::vec2(-3.f, 3.f));
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, numberEnt](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    m_targetIndex = static_cast<std::int32_t>((m_targetIndex + m_holeData.size()) % (m_holeData.size() + 1));
                    std::string str = (m_targetIndex - 1) < 0 ? "?" : std::to_string(m_targetIndex);
                    numberEnt.getComponent<cro::Text>().setString(str);
                    centreText(numberEnt);

                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                }
            });
    countEnt.getComponent<cro::Transform>().addChild(buttonEnt.getComponent<cro::Transform>());

    buttonEnt = createButton("arrow_right", glm::vec2(35.f, 3.f));
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, numberEnt](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    m_targetIndex = (m_targetIndex + 1) % (m_holeData.size() + 1);
                    std::string str = (m_targetIndex - 1) < 0 ? "?" : std::to_string(m_targetIndex);
                    numberEnt.getComponent<cro::Text>().setString(str);
                    centreText(numberEnt);

                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                }
            });
    countEnt.getComponent<cro::Transform>().addChild(buttonEnt.getComponent<cro::Transform>());

#ifdef USE_GNS

    cro::SpriteSheet leaderSheet;
    leaderSheet.loadFromFile("assets/golf/sprites/driving_leaderboard.spt", m_resources.textures);

    struct LeaderboardData final
    {
        float progress = 0.f;
        int direction = 0;
    };

    //scoreboard window
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 6.f, 13.f, 0.8f });
    entity.getComponent<cro::Transform>().setScale({ 0.f, 0.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = leaderSheet.getSprite("board");
    auto b = entity.getComponent<cro::Sprite>().getTextureRect();
    entity.getComponent<cro::Transform>().setOrigin({ b.width / 2.f, b.height / 2.f });
    entity.getComponent<cro::Transform>().move(entity.getComponent<cro::Transform>().getOrigin());
    entity.addComponent<cro::Callback>().setUserData<LeaderboardData>();
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        const float Speed = dt * 4.f;
        auto& [progress, direction] = e.getComponent<cro::Callback>().getUserData<LeaderboardData>();
        if (direction == 0)
        {
            //grow
            progress = std::min(1.f, progress + Speed);
            if (progress == 1)
            {
                direction = 1;
                e.getComponent<cro::Callback>().active = false;
                m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Leaderboard);
            }
        }
        else
        {
            //shrink
            progress = std::max(0.f, progress - Speed);
            if (progress == 0)
            {
                direction = 0;
                e.getComponent<cro::Callback>().active = false;
                m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Options);
            }
        }
        e.getComponent<cro::Transform>().setScale({ cro::Util::Easing::easeOutQuad(progress), 1.f });
    };
    bgEntity.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    auto lbEntity = entity;
    m_leaderboardEntity = entity;

    auto textSelected = uiSystem->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Text>().setFillColour(TextHighlightColour);
            e.getComponent<cro::AudioEmitter>().play();
        });
    auto textUnselected = uiSystem->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
        });


    //close leaderboards
    textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ 352.f, 11.f, 0.12f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    textEnt4.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
    textEnt4.getComponent<cro::Text>().setString("Back");
    auto textBounds = cro::Text::getLocalBounds(textEnt4);
    textEnt4.addComponent<cro::AudioEmitter>() = as.getEmitter("switch");
    textEnt4.addComponent<cro::UIInput>().setGroup(MenuID::Leaderboard);
    textEnt4.getComponent<cro::UIInput>().area = textBounds;
    textEnt4.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = textSelected;
    textEnt4.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = textUnselected;
    textEnt4.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, uiSystem, lbEntity](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    uiSystem->setActiveGroup(MenuID::Dummy);
                    lbEntity.getComponent<cro::Callback>().active = true;

                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                }
            });
    lbEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());
    
    //name column
    textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ 94.f, 234.f, 0.12f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    textEnt4.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
    textEnt4.getComponent<cro::Text>().setVerticalSpacing(LeaderboardTextSpacing);
    lbEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());
    auto nameColumn = textEnt4;

    //rank column
    textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ 38.f, 234.f, 0.12f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    textEnt4.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
    textEnt4.getComponent<cro::Text>().setVerticalSpacing(LeaderboardTextSpacing);
    lbEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());
    auto rankColumn = textEnt4;

    //score column
    textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ 294.f, 234.f, 0.12f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    textEnt4.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
    textEnt4.getComponent<cro::Text>().setVerticalSpacing(LeaderboardTextSpacing);
    lbEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());
    auto scoreColumn = textEnt4;
    
    //id display
    textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ b.width / 2.f, 11.f, 0.12f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    textEnt4.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
    textEnt4.getComponent<cro::Text>().setString("Best of 5");
    lbEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());
    auto scoreType = textEnt4;

    //rank display
    textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ b.width / 2.f, 262.f, 0.12f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    textEnt4.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
    textEnt4.getComponent<cro::Text>().setString("Global");
    centreText(textEnt4);
    lbEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());
    auto rankType = textEnt4;

    auto updateDisplay = [nameColumn, scoreColumn, rankColumn, scoreType, rankType]() mutable
    {
        const std::array ScoreStrings =
        {
            "Best Of 5",
            "Best Of 9",
            "Best Of 18",
        };
        const std::array RankStrings =
        {
            "Global",
            "Nearest",
            "Friends"
        };
        scoreType.getComponent<cro::Text>().setString(ScoreStrings[leaderboardID]);
        rankType.getComponent<cro::Text>().setString(RankStrings[leaderboardFilter]);
        centreText(scoreType);
        centreText(rankType);

        auto scores = Social::getLeaderboardResults(leaderboardID, leaderboardFilter);
        rankColumn.getComponent<cro::Text>().setString(scores[0]);
        nameColumn.getComponent<cro::Text>().setString(scores[1]);
        scoreColumn.getComponent<cro::Text>().setString(scores[2]);
    };
    

    //browse leaderboards
    textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ (bounds.width / 5.f) * 4.f, 67.f, 0.02f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Sprite>() = spriteSheet.getSprite("leaderboard_button");

    auto uBounds = spriteSheet.getSprite("leaderboard_button").getTextureRect();
    auto sBounds = spriteSheet.getSprite("leaderboard_highlight").getTextureRect();
    auto buttonBounds = textEnt4.getComponent<cro::Sprite>().getTextureBounds();
    textEnt4.getComponent<cro::Transform>().setOrigin({ std::floor(buttonBounds.width / 2.f), buttonBounds.height });
    textEnt4.addComponent<cro::AudioEmitter>() = as.getEmitter("switch");
    textEnt4.addComponent<cro::UIInput>().setGroup(MenuID::Options);
    textEnt4.getComponent<cro::UIInput>().area = buttonBounds;
    textEnt4.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] =
        uiSystem->addCallback(
            [sBounds](cro::Entity e)
            {
                e.getComponent<cro::Sprite>().setTextureRect(sBounds);
                e.getComponent<cro::AudioEmitter>().play();
            });
        
    textEnt4.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] =
        uiSystem->addCallback(
            [uBounds](cro::Entity e)
            {
                e.getComponent<cro::Sprite>().setTextureRect(uBounds);
            });
    textEnt4.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, uiSystem, bgEntity, lbEntity, updateDisplay](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                const auto& [state, _] = bgEntity.getComponent<cro::Callback>().getUserData<MessageAnim>();
                if (state == MessageAnim::Hold
                    && activated(evt))
                {
                    uiSystem->setActiveGroup(MenuID::Dummy);
                    lbEntity.getComponent<cro::Callback>().active = true;
                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();

                    updateDisplay();
                }
            });

    bgEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());


    //arrow buttons
    auto buttonSelected = uiSystem->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Sprite>().setColour(cro::Colour::White);
            e.getComponent<cro::AudioEmitter>().play();
        });
    auto buttonUnselected = uiSystem->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        });

    auto createArrow =
        [&](glm::vec2 position, const std::string& spriteName)
    {
        auto e = m_uiScene.createEntity();
        e.addComponent<cro::Transform>().setPosition(glm::vec3(position , 0.1f));
        e.addComponent<cro::AudioEmitter>() = as.getEmitter("switch");
        e.addComponent<cro::Drawable2D>();
        e.addComponent<cro::Sprite>() = leaderSheet.getSprite(spriteName);
        e.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        auto b = e.getComponent<cro::Sprite>().getTextureBounds();
        e.addComponent<cro::UIInput>().area = b;
        e.getComponent<cro::UIInput>().setGroup(MenuID::Leaderboard);
        e.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = buttonSelected;
        e.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = buttonUnselected;

        lbEntity.getComponent<cro::Transform>().addChild(e.getComponent<cro::Transform>());

        return e;
    };
    //filter left
    entity = createArrow(glm::vec2(140.f, 250.f), "arrow_left");
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, updateDisplay](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    leaderboardFilter = (leaderboardFilter + (MaxLeaderboardFilter - 1)) % MaxLeaderboardFilter;
                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                    updateDisplay();
                }
            });

    //filter right
    entity = createArrow(glm::vec2(232.f, 250.f), "arrow_right");
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, updateDisplay](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    leaderboardFilter = (leaderboardFilter + 1) % MaxLeaderboardFilter;
                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                    updateDisplay();
                }
            });

    //score left
    entity = createArrow(glm::vec2(140.f, -2.f), "arrow_left");
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, updateDisplay](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    leaderboardID = (leaderboardID + (MaxLeaderboardFilter - 1)) % MaxLeaderboardFilter;
                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                    updateDisplay();
                }
            });

    //score right
    entity =createArrow(glm::vec2(232.f, -2.f), "arrow_right");
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, updateDisplay](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    leaderboardID = (leaderboardID + 1) % MaxLeaderboardFilter;
                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                    updateDisplay();
                }
            });

#endif

    //start button
    auto selectedBounds = spriteSheet.getSprite("start_highlight").getTextureRect();
    auto unselectedBounds = spriteSheet.getSprite("start_button").getTextureRect();
    auto startButton = m_uiScene.createEntity();
    startButton.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 34.f, 0.2f });
    startButton.addComponent<cro::Drawable2D>();
    startButton.addComponent<cro::Sprite>() = spriteSheet.getSprite("start_button");
    startButton.addComponent<cro::AudioEmitter>() = as.getEmitter("switch");
    startButton.addComponent<cro::UIInput>().setGroup(MenuID::Options);
    startButton.getComponent<cro::UIInput>().area = startButton.getComponent<cro::Sprite>().getTextureBounds();
    startButton.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] =
        uiSystem->addCallback(
            [selectedBounds](cro::Entity e)
            {
                e.getComponent<cro::Sprite>().setTextureRect(selectedBounds);
                e.getComponent<cro::Transform>().setOrigin({ selectedBounds.width / 2.f, selectedBounds.height / 2.f });
                e.getComponent<cro::AudioEmitter>().play();
            });
    startButton.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] =
        uiSystem->addCallback(
            [unselectedBounds](cro::Entity e)
            {
                e.getComponent<cro::Sprite>().setTextureRect(unselectedBounds);
                e.getComponent<cro::Transform>().setOrigin({ unselectedBounds.width / 2.f, unselectedBounds.height / 2.f });
            });
    startButton.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, uiSystem, bgEntity](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                auto& [state, timeout] = bgEntity.getComponent<cro::Callback>().getUserData<MessageAnim>();
                if (state == MessageAnim::Hold
                    && activated(evt))
                {
                    state = MessageAnim::Close;
                    timeout = 1.f;
                    uiSystem->setActiveGroup(MenuID::Dummy);
                    
                    m_gameScene.getSystem<BallSystem>()->forceWindChange();
                    m_gameScene.getDirector<DrivingRangeDirector>()->setHoleCount(m_strokeCounts[m_strokeCountIndex], m_targetIndex - 1);

                    setHole(m_gameScene.getDirector<DrivingRangeDirector>()->getCurrentHole());

                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();

                    //hide the black fade.
                    m_summaryScreen.fadeEnt.getComponent<cro::Callback>().setUserData<float>(0.f);
                    m_summaryScreen.fadeEnt.getComponent<cro::Callback>().active = true;

                    m_mapTexture.clear(cro::Colour::Transparent);
                    m_mapTexture.display();

                    //reset stat timer
                    m_statClock.restart();
                }
            });
    centreSprite(startButton);
    bgEntity.getComponent<cro::Transform>().addChild(startButton.getComponent<cro::Transform>());


    //wang this in here so we can debug easier
    /*cro::Command cmd;
    cmd.targetFlags = CommandID::UI::DrivingBoard;
    cmd.action = [](cro::Entity e, float) {e.getComponent<cro::Callback>().active = true; };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);*/
}

void DrivingState::createSummary()
{
    auto* uiSystem = m_uiScene.getSystem<cro::UISystem>();

    //black fade
    auto fadeEnt = m_uiScene.createEntity();
    fadeEnt.addComponent<cro::Transform>().setPosition({ 0.f, 0.f, 1.f });
    fadeEnt.addComponent<cro::Drawable2D>().setVertexData(
        {
        cro::Vertex2D(glm::vec2(0.f, 1.f), cro::Colour::Transparent),
        cro::Vertex2D(glm::vec2(0.f), cro::Colour::Transparent),
        cro::Vertex2D(glm::vec2(1.f), cro::Colour::Transparent),
        cro::Vertex2D(glm::vec2(1.f, 0.f), cro::Colour::Transparent)
        }
    );
    fadeEnt.addComponent<cro::Callback>().setUserData<float>(BackgroundAlpha);
    fadeEnt.getComponent<cro::Callback>().function =
        [](cro::Entity e, float dt)
    {
        auto size = glm::vec2(cro::App::getWindow().getSize());
        e.getComponent<cro::Transform>().setScale(size);

        auto& verts = e.getComponent<cro::Drawable2D>().getVertexData();
        auto a = verts[0].colour.getAlpha();

        auto target = e.getComponent<cro::Callback>().getUserData<float>();
        const float step = dt * 2.f;

        if (a < target)
        {
            a = std::min(target, a + step);
        }
        else
        {
            a = std::max(target, a - step);

            if (a == 0)
            {
                e.getComponent<cro::Callback>().setUserData<float>(BackgroundAlpha);
                e.getComponent<cro::Callback>().active = false;
                e.getComponent<cro::Transform>().setPosition({ -10000.f, -10000.f });
            }
        }

        for (auto& v : verts)
        {
            v.colour.setAlpha(a);
        }
    };
    m_summaryScreen.fadeEnt = fadeEnt;

    //background
    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/scoreboard.spt", m_resources.textures);
    auto bgSprite = spriteSheet.getSprite("border");

    auto bounds = bgSprite.getTextureBounds();
    auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
    auto position = glm::vec3(size.x / 2.f, size.y / 2.f, 1.5f);

    auto bgEntity = m_uiScene.createEntity();
    bgEntity.addComponent<cro::Transform>().setPosition(position);
    bgEntity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
    bgEntity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    bgEntity.addComponent<cro::Drawable2D>();
    bgEntity.addComponent<cro::Sprite>() = bgSprite;
    bgEntity.addComponent<cro::Callback>().setUserData<MessageAnim>();
    bgEntity.getComponent<cro::Callback>().function = MenuCallback(m_viewScale, uiSystem, MenuID::Summary);
    

    auto& smallFont = m_sharedData.sharedResources->fonts.get(FontID::Info);
    auto& largeFont = m_sharedData.sharedResources->fonts.get(FontID::UI);

    //title
    auto titleText = m_uiScene.createEntity();
    titleText.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 293.f, 0.02f });
    titleText.addComponent<cro::Drawable2D>();
    titleText.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    titleText.getComponent<cro::Text>().setFillColour(TextNormalColour);
    titleText.getComponent<cro::Text>().setString("Summary");
    centreText(titleText);
    bgEntity.getComponent<cro::Transform>().addChild(titleText.getComponent<cro::Transform>());


    //info text
    auto infoEnt = m_uiScene.createEntity();
    infoEnt.addComponent<cro::Transform>().setPosition({ SummaryOffset, SummaryHeight, 0.02f });
    infoEnt.addComponent<cro::Drawable2D>();
    infoEnt.addComponent<cro::Text>(smallFont).setString("Sample Text\n1\n1\n1\n1\n1\n1\n1\n1");
    infoEnt.getComponent<cro::Text>().setCharacterSize(InfoTextSize);
    infoEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    bgEntity.getComponent<cro::Transform>().addChild(infoEnt.getComponent<cro::Transform>());
    m_summaryScreen.text01 = infoEnt;

    infoEnt = m_uiScene.createEntity();
    infoEnt.addComponent<cro::Transform>().setPosition({ (bounds.width / 2.f) + SummaryOffset, SummaryHeight, 0.02f });
    infoEnt.addComponent<cro::Drawable2D>();
    infoEnt.addComponent<cro::Text>(smallFont).setString("Sample Text\n1\n1\n1\n1\n1\n1\n1\n1");
    infoEnt.getComponent<cro::Text>().setCharacterSize(InfoTextSize);
    infoEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    bgEntity.getComponent<cro::Transform>().addChild(infoEnt.getComponent<cro::Transform>());
    m_summaryScreen.text02 = infoEnt;

    auto summaryEnt = m_uiScene.createEntity();
    summaryEnt.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 120.f, 0.02f });
    summaryEnt.addComponent<cro::Drawable2D>();
    summaryEnt.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    summaryEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    summaryEnt.getComponent<cro::Text>().setString("Placeholder Text");
    centreText(summaryEnt);
    bgEntity.getComponent<cro::Transform>().addChild(summaryEnt.getComponent<cro::Transform>());
    m_summaryScreen.summary = summaryEnt;


    cro::AudioScape as;
    as.loadFromFile("assets/golf/sound/menu.xas", m_resources.audio);
    m_summaryScreen.audioEnt = m_uiScene.createEntity();
    m_summaryScreen.audioEnt.addComponent<cro::Transform>();
    m_summaryScreen.audioEnt.addComponent<cro::AudioEmitter>() = as.getEmitter("accept");

    //star ratings
    const float starWidth = spriteSheet.getSprite("star").getTextureBounds().width;
    glm::vec3 pos(std::floor((bounds.width / 2.f) - (starWidth * 1.5f)), 76.f, 0.02f);
    for (auto i = 0; i < 3; ++i)
    {
        auto entity = m_uiScene.createEntity();
        entity.addComponent<cro::Transform>().setPosition(pos);
        entity.addComponent<cro::Drawable2D>();
        entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("star");
        entity.addComponent<cro::SpriteAnimation>().play(0);
        entity.addComponent<cro::AudioEmitter>() = as.getEmitter("star");
        entity.addComponent<cro::ParticleEmitter>().settings.loadFromFile("assets/golf/particles/spark.xyp", m_resources.textures);
        entity.addComponent<cro::Callback>().function = StarAnimCallback(1.5f + (i * 0.5f));
        bgEntity.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

        m_summaryScreen.stars[i] = entity;
        pos.x += starWidth;
    }


    //high score text
    auto textEnt4 = m_uiScene.createEntity();
    textEnt4.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 32.f, 0.02f });
    textEnt4.addComponent<cro::Drawable2D>();
    textEnt4.addComponent<cro::Text>(smallFont).setCharacterSize(InfoTextSize);
    textEnt4.getComponent<cro::Text>().setFillColour(TextNormalColour);
    textEnt4.getComponent<cro::Text>().setString("New Personal Best!");
    textEnt4.addComponent<cro::Callback>().setUserData<float>(0.f);
    textEnt4.getComponent<cro::Callback>().function =
        [](cro::Entity e, float dt)
    {
        auto& currTime = e.getComponent<cro::Callback>().getUserData<float>();
        currTime -= dt;
        if (currTime < 0)
        {
            currTime += 1.f;

            auto c = e.getComponent<cro::Text>().getFillColour();
            c.setAlpha(c.getAlpha() > 0 ? 0.f : 1.f);
            e.getComponent<cro::Text>().setFillColour(c);
        }
    };
    centreText(textEnt4);
    bgEntity.getComponent<cro::Transform>().addChild(textEnt4.getComponent<cro::Transform>());
    m_summaryScreen.bestMessage = textEnt4;

    //replay text
    auto questionEnt = m_uiScene.createEntity();
    questionEnt.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 72.f, 0.02f });
    questionEnt.addComponent<cro::Drawable2D>();
    questionEnt.addComponent<cro::Text>(largeFont).setString("Play Again?");
    questionEnt.getComponent<cro::Text>().setCharacterSize(UITextSize);
    questionEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    centreText(questionEnt);
    bgEntity.getComponent<cro::Transform>().addChild(questionEnt.getComponent<cro::Transform>());

    const auto centreSprite = [](cro::Entity e)
    {
        auto bounds = e.getComponent<cro::Sprite>().getTextureBounds();
        e.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    };

    auto selectedBounds = spriteSheet.getSprite("yes_highlight").getTextureRect();
    auto unselectedBounds = spriteSheet.getSprite("yes_button").getTextureRect();

    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ (bounds.width / 2.f) - 22.f, 48.f, 0.2f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("yes_button");
    entity.addComponent<cro::AudioEmitter>() = as.getEmitter("switch");
    entity.addComponent<cro::UIInput>().setGroup(MenuID::Summary);
    entity.getComponent<cro::UIInput>().area = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] =
        uiSystem->addCallback(
            [selectedBounds](cro::Entity e)
            {
                e.getComponent<cro::Sprite>().setTextureRect(selectedBounds);
                e.getComponent<cro::Transform>().setOrigin({ selectedBounds.width / 2.f, selectedBounds.height / 2.f });
                e.getComponent<cro::AudioEmitter>().play();
            });
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] =
        uiSystem->addCallback(
            [unselectedBounds](cro::Entity e)
            {
                e.getComponent<cro::Sprite>().setTextureRect(unselectedBounds);
                e.getComponent<cro::Transform>().setOrigin({ unselectedBounds.width / 2.f, unselectedBounds.height / 2.f });
            });
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, bgEntity](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                auto& [state, timeout] = bgEntity.getComponent<cro::Callback>().getUserData<MessageAnim>();
                if (state == MessageAnim::Hold
                    && activated(evt))
                {
                    auto c = TextNormalColour;
                    c.setAlpha(0.f);
                    m_summaryScreen.bestMessage.getComponent<cro::Text>().setFillColour(c);
                    m_summaryScreen.bestMessage.getComponent<cro::Callback>().active = false;

                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                    state = MessageAnim::Close;
                    timeout = 1.f;

                    for (auto star : m_summaryScreen.stars)
                    {
                        star.getComponent<cro::SpriteAnimation>().play(0);
                    }

                    cro::Command cmd;
                    cmd.targetFlags = CommandID::UI::DrivingBoard;
                    cmd.action = [](cro::Entity e, float) {e.getComponent<cro::Callback>().active = true; };
                    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

                    //fade background should already be visible from showing summary
                    /*m_summaryScreen.fadeEnt.getComponent<cro::Callback>().setUserData<float>(BackgroundAlpha);
                    m_summaryScreen.fadeEnt.getComponent<cro::Callback>().active = true;
                    m_summaryScreen.fadeEnt.getComponent<cro::Transform>().setPosition({ 0.f, 0.f, FadeDepth });*/
                }
            });
    centreSprite(entity);
    bgEntity.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    selectedBounds = spriteSheet.getSprite("no_highlight").getTextureRect();
    unselectedBounds = spriteSheet.getSprite("no_button").getTextureRect();

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ (bounds.width / 2.f) + 22.f, 48.f, 0.2f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("no_button");
    entity.addComponent<cro::AudioEmitter>() = as.getEmitter("switch");
    entity.addComponent<cro::UIInput>().setGroup(MenuID::Summary);
    entity.getComponent<cro::UIInput>().area = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] =
        uiSystem->addCallback(
            [selectedBounds](cro::Entity e)
            {
                e.getComponent<cro::Sprite>().setTextureRect(selectedBounds);
                e.getComponent<cro::Transform>().setOrigin({ selectedBounds.width / 2.f, selectedBounds.height / 2.f });
                e.getComponent<cro::AudioEmitter>().play();
            });
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] =
        uiSystem->addCallback(
            [unselectedBounds](cro::Entity e)
            {
                e.getComponent<cro::Sprite>().setTextureRect(unselectedBounds);
                e.getComponent<cro::Transform>().setOrigin({ unselectedBounds.width / 2.f, unselectedBounds.height / 2.f });
            });
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        uiSystem->addCallback(
            [&, bgEntity](cro::Entity e, const cro::ButtonEvent& evt)
            {
                auto& [state, timeout] = bgEntity.getComponent<cro::Callback>().getUserData<MessageAnim>();
                if (state == MessageAnim::Hold
                    && activated(evt))
                {
                    m_summaryScreen.audioEnt.getComponent<cro::AudioEmitter>().play();
                    requestStackClear();
                    requestStackPush(StateID::Menu);
                }
            });
    centreSprite(entity);
    bgEntity.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    m_summaryScreen.root = bgEntity;
}

void DrivingState::updateMinimap()
{
    auto oldCam = m_gameScene.setActiveCamera(m_mapCam);

    m_mapTexture.clear(TextNormalColour);
    m_gameScene.render();

    auto holePos = m_holeData[m_gameScene.getDirector<DrivingRangeDirector>()->getCurrentHole()].pin / 2.f;
    m_flagQuad.setPosition({ holePos.x, -holePos.z });
    m_flagQuad.move(RangeSize / 4.f);
    m_flagQuad.draw();

    m_mapTexture.display();

    m_gameScene.setActiveCamera(oldCam);
}

void DrivingState::updateWindDisplay(glm::vec3 direction)
{
    float rotation = std::atan2(-direction.z, direction.x);
    static constexpr float CamRotation = cro::Util::Const::PI / 2.f;

    cro::Command cmd;
    cmd.targetFlags = CommandID::UI::WindSock;
    cmd.action = [&, rotation](cro::Entity e, float dt)
    {
        auto r = rotation - CamRotation;

        float& currRotation = e.getComponent<float>();
        currRotation += cro::Util::Maths::shortestRotation(currRotation, r) * dt;
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

    m_gameScene.getSystem<CloudSystem>()->setWindVector(direction);
}

void DrivingState::showMessage(float range)
{
    const auto* director = m_gameScene.getDirector<DrivingRangeDirector>();
    float score = director->getScore(director->getCurrentStroke() - 1); //this was incremented internally when score was updated

    auto bounds = m_sprites[SpriteID::MessageBoard].getTextureBounds();
    auto size = glm::vec2(GolfGame::getActiveTarget()->getSize());
    auto position = glm::vec3(size.x / 2.f, size.y / 2.f, 0.05f);

    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition(position);
    entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, 0.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::MessageBoard];
    entity.addComponent<cro::CommandTarget>().ID = CommandID::UI::MessageBoard;

    std::uint8_t starCount = 0;

    auto& largeFont = m_sharedData.sharedResources->fonts.get(FontID::UI);
    auto textEnt = m_uiScene.createEntity();
    textEnt.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 56.f, 0.02f });
    textEnt.addComponent<cro::Drawable2D>();
    textEnt.addComponent<cro::Text>(largeFont).setCharacterSize(UITextSize);
    textEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
    if (score < BadScore)
    {
        textEnt.getComponent<cro::Text>().setString("Bad Luck!");
    }
    else if (score < GoodScore)
    {
        textEnt.getComponent<cro::Text>().setString("Good Effort!");
        starCount = 1;
        Social::awardXP(XPValues[XPID::Good]);
    }
    else if (score < ExcellentScore)
    {
        textEnt.getComponent<cro::Text>().setString("Not Bad!");
        starCount = 2;
        Social::awardXP(XPValues[XPID::NotBad]);
    }
    else
    {
        textEnt.getComponent<cro::Text>().setString("Excellent!");
        starCount = 3;
        Social::awardXP(XPValues[XPID::Excellent]);
    }
    centreText(textEnt);
    entity.getComponent<cro::Transform>().addChild(textEnt.getComponent<cro::Transform>());


    auto& smallFont = m_sharedData.sharedResources->fonts.get(FontID::Info);

    std::stringstream s1;
    s1.precision(3);
    if (m_sharedData.imperialMeasurements)
    {
        s1 << range * 1.094f << "y";
    }
    else
    {
        s1 << range << "m";
    }

    auto textEnt2 = m_uiScene.createEntity();
    textEnt2.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 36.f, 0.02f });
    textEnt2.addComponent<cro::Drawable2D>();
    textEnt2.addComponent<cro::Text>(smallFont).setCharacterSize(InfoTextSize);
    textEnt2.getComponent<cro::Text>().setFillColour(TextNormalColour);
    textEnt2.getComponent<cro::Text>().setString("Range: " + s1.str());
    centreText(textEnt2);
    entity.getComponent<cro::Transform>().addChild(textEnt2.getComponent<cro::Transform>());

    std::stringstream s2;
    s2.precision(3);
    s2 << "Accuracy: " << score << "%";

    auto textEnt3 = m_uiScene.createEntity();
    textEnt3.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, 24.f, 0.02f });
    textEnt3.addComponent<cro::Drawable2D>();
    textEnt3.addComponent<cro::Text>(smallFont).setCharacterSize(InfoTextSize);
    textEnt3.getComponent<cro::Text>().setFillColour(TextNormalColour);
    textEnt3.getComponent<cro::Text>().setString(s2.str());
    centreText(textEnt3);
    entity.getComponent<cro::Transform>().addChild(textEnt3.getComponent<cro::Transform>());


    //add mini graphic showing rank
    auto imgEnt = m_uiScene.createEntity();
    imgEnt.addComponent<cro::Transform>().setPosition({ bounds.width / 2.f, bounds.height / 2.f, 0.02f });
    imgEnt.getComponent<cro::Transform>().move(glm::vec2(0.f, 2.f));
    imgEnt.addComponent<cro::Drawable2D>();
    imgEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::Stars];
    imgEnt.addComponent<cro::SpriteAnimation>();
    bounds = imgEnt.getComponent<cro::Sprite>().getTextureBounds();
    imgEnt.getComponent<cro::Transform>().setOrigin({ std::floor(bounds.width / 2.f), std::floor(bounds.height / 2.f), 0.f });
    entity.getComponent<cro::Transform>().addChild(imgEnt.getComponent<cro::Transform>());


    //callback for anim/self destruction
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<MessageAnim>();
    entity.getComponent<cro::Callback>().function =
        [&, textEnt, textEnt2, textEnt3, imgEnt, starCount](cro::Entity e, float dt) mutable
    {
        static constexpr float HoldTime = 4.f;
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
                imgEnt.getComponent<cro::SpriteAnimation>().play(starCount);
            }
            break;
        case MessageAnim::Hold:
            //hold
            currTime = std::min(HoldTime, currTime + dt);

            if (currTime > (HoldTime / 2.f))
            {
                //this should be safe to call repeatedly
                m_gameScene.getSystem<CameraFollowSystem>()->resetCamera();
            }

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

                if (m_gameScene.getDirector<DrivingRangeDirector>()->roundEnded())
                {
                    //show summary screen
                    const auto* director = m_gameScene.getDirector<DrivingRangeDirector>();
                    auto scoreCount = director->getTotalStrokes();
                    float totalScore = 0.f;

                    std::string summary;
                    for (auto i = 0; i < std::min(9, scoreCount); ++i)
                    {
                        float score = director->getScore(i);

                        std::stringstream s;
                        s.precision(3);
                        s << "Turn " << i + 1 << ":      " << score << "%\n";
                        summary += s.str();

                        totalScore += score;
                    }
                    m_summaryScreen.text01.getComponent<cro::Text>().setString(summary);
                    summary.clear();

                    //second column
                    if (scoreCount > 9)
                    {
                        for (auto i = 9; i < scoreCount; ++i)
                        {
                            float score = director->getScore(i);

                            std::stringstream s;
                            s.precision(3);
                            s << "Turn " << i + 1 << ":      " << score << "%\n";
                            summary += s.str();

                            totalScore += score;
                        }
                        m_summaryScreen.text02.getComponent<cro::Text>().setString(summary);

                        auto& tx = m_summaryScreen.text01.getComponent<cro::Transform>();
                        auto pos = tx.getPosition();
                        pos.x = SummaryOffset;
                        tx.setPosition(pos);
                        tx.setOrigin({ 0.f, 0.f });
                    }
                    else
                    {
                        auto& tx = m_summaryScreen.text01.getComponent<cro::Transform>();
                        auto pos = tx.getPosition();
                        pos.x = 200.f; //TODO this should be half background width
                        tx.setPosition(pos);
                        centreText(m_summaryScreen.text01);

                        m_summaryScreen.text02.getComponent<cro::Text>().setString(" ");
                    }

                    totalScore /= scoreCount;
                    std::stringstream s;
                    s.precision(3);
                    s << "\nTotal: " << totalScore << "% - ";

                    if (totalScore < BadScore)
                    {
                        s << "Maybe more practice..?";
                    }
                    else if (totalScore < GoodScore)
                    {
                        s << "Could Do Better...";
                        m_summaryScreen.stars[0].getComponent<cro::Callback>().active = true;
                        Achievements::awardAchievement(AchievementStrings[AchievementID::BronzeStar]);
                    }
                    else if (totalScore < ExcellentScore)
                    {
                        s << "Great Job!";
                        m_summaryScreen.stars[0].getComponent<cro::Callback>().active = true;
                        m_summaryScreen.stars[1].getComponent<cro::Callback>().active = true;
                        Achievements::awardAchievement(AchievementStrings[AchievementID::SilverStar]);
                    }
                    else
                    {
                        s << "Excellent!";
                        m_summaryScreen.stars[0].getComponent<cro::Callback>().active = true;
                        m_summaryScreen.stars[1].getComponent<cro::Callback>().active = true;
                        m_summaryScreen.stars[2].getComponent<cro::Callback>().active = true;
                        Achievements::awardAchievement(AchievementStrings[AchievementID::GoldStar]);
                    }

                    m_summaryScreen.summary.getComponent<cro::Text>().setString(s.str());
                    centreText(m_summaryScreen.summary);
                    
                    m_summaryScreen.fadeEnt.getComponent<cro::Transform>().setPosition({0.f, 0.f, FadeDepth});
                    m_summaryScreen.fadeEnt.getComponent<cro::Callback>().setUserData<float>(BackgroundAlpha);
                    m_summaryScreen.fadeEnt.getComponent<cro::Callback>().active = true;
                    m_summaryScreen.root.getComponent<cro::Callback>().active = true;

                    if (totalScore > m_topScores[m_strokeCountIndex])
                    {
                        m_topScores[m_strokeCountIndex] = totalScore;
                        saveScores();
                        m_summaryScreen.bestMessage.getComponent<cro::Callback>().active = true;
                    }
                    else
                    {
                        auto c = TextNormalColour;
                        c.setAlpha(0.f);
                        m_summaryScreen.bestMessage.getComponent<cro::Text>().setFillColour(c);
                    }


                    //reset the minimap
                    auto oldCam = m_gameScene.setActiveCamera(m_mapCam);
                    m_mapTexture.clear(TextNormalColour);
                    m_gameScene.render();
                    m_mapTexture.display();
                    m_gameScene.setActiveCamera(oldCam);

                    //update the stat
                    Achievements::incrementStat(StatStrings[StatID::TimeOnTheRange], m_statClock.elapsed().asSeconds());
                }
                else
                {
                    setHole(m_gameScene.getDirector<DrivingRangeDirector>()->getCurrentHole());
                }
            }
            break;
        }
    };


    //raise a message for sound effects etc
    auto* msg = getContext().appInstance.getMessageBus().post<GolfEvent>(MessageID::GolfMessage);
    msg->type = GolfEvent::DriveComplete;
    msg->score = starCount;
}

void DrivingState::floatingMessage(const std::string& msg)
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