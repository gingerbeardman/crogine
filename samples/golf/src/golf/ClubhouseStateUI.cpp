/*-----------------------------------------------------------------------

Matt Marchant 2022
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

#include "ClubhouseState.hpp"
#include "CommandIDs.hpp"
#include "GameConsts.hpp"
#include "MenuConsts.hpp"
#include "MenuCallbacks.hpp"
#include "PacketIDs.hpp"
#include "Utility.hpp"
#include "NameScrollSystem.hpp"
#include "../GolfGame.hpp"

#include <crogine/ecs/components/Camera.hpp>
#include <crogine/ecs/components/Callback.hpp>
#include <crogine/ecs/components/Sprite.hpp>
#include <crogine/ecs/components/SpriteAnimation.hpp>
#include <crogine/ecs/components/Drawable2D.hpp>
#include <crogine/ecs/components/Text.hpp>
#include <crogine/ecs/components/CommandTarget.hpp>
#include <crogine/ecs/components/AudioEmitter.hpp>
#include <crogine/ecs/components/UIInput.hpp>

#include <crogine/graphics/SpriteSheet.hpp>
#include <crogine/util/Random.hpp>
#include <crogine/util/String.hpp>

#include <iomanip>

namespace
{
#include "RandNames.hpp"

    constexpr std::size_t MaxGameIndices = 3;
}

void ClubhouseState::createUI()
{
    auto mouseEnterCallback = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e) mutable
        {
            e.getComponent<cro::Text>().setFillColour(TextGoldColour);
            e.getComponent<cro::AudioEmitter>().play();
        });
    auto mouseExitCallback = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Text>().setFillColour(TextNormalColour);
        });


    //displays the background
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, 0.f, -0.5f });
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


    //menus are attached to this
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::RootNode;
    auto rootNode = entity;

    //consumes input during menu animation.
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::UIInput>().setGroup(MenuID::Dummy);

    createMainMenu(rootNode, mouseEnterCallback, mouseExitCallback);
    createAvatarMenu(rootNode, mouseEnterCallback, mouseExitCallback);
#ifdef USE_GNS
    createBrowserMenu(rootNode, mouseEnterCallback, mouseExitCallback);
#else
    createJoinMenu(rootNode, mouseEnterCallback, mouseExitCallback);
#endif
    createLobbyMenu(rootNode, mouseEnterCallback, mouseExitCallback);

    //hack to activate main menu - this will eventually be done
    //by animation callback
    //LATER NOTE - I forget why this was, but it works so I'm leaving it here.
    cro::Command cmd;
    cmd.targetFlags = CommandID::Menu::RootNode;
    cmd.action = [&](cro::Entity, float)
    {
        m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Main);
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    //ui viewport is set 1:1 with window, then the scene
    //is scaled to best-fit to maintain pixel accuracy of text.
    auto updateView = [&, rootNode, courseEnt](cro::Camera& cam) mutable
    {
        auto windowSize = GolfGame::getActiveTarget()->getSize();
        glm::vec2 size(windowSize);

        cam.setOrthographic(0.f, size.x, 0.f, size.y, -2.f, 10.f);
        cam.viewport = { 0.f, 0.f, 1.f, 1.f };

        auto vpSize = calcVPSize();

        m_viewScale = glm::vec2(std::floor(size.y / vpSize.y));
        rootNode.getComponent<cro::Transform>().setScale(m_viewScale);
        rootNode.getComponent<cro::Transform>().setPosition(m_menuPositions[m_currentMenu] * m_viewScale);

        glm::vec2 courseScale(m_sharedData.pixelScale ? m_viewScale.x : 1.f);
        courseEnt.getComponent<cro::Transform>().setScale(courseScale);
        courseEnt.getComponent<cro::Callback>().active = true; //makes sure to delay so updating the texture size is complete first
        courseEnt.getComponent<cro::Transform>().setPosition(glm::vec3(size / 2.f, -1.f));

        //updates any text objects / buttons with a relative position
        cro::Command cmd;
        cmd.targetFlags = CommandID::Menu::UIElement;
        cmd.action =
            [&, size](cro::Entity e, float)
        {
            const auto& element = e.getComponent<UIElement>();
            auto pos = element.absolutePosition;
            pos += element.relativePosition * size / m_viewScale;

            pos.x = std::floor(pos.x);
            pos.y = std::floor(pos.y);

            e.getComponent<cro::Transform>().setPosition(glm::vec3(pos, element.depth));
        };
        m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
    };

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Camera>().resizeCallback = updateView;
    m_uiScene.setActiveCamera(entity);
    updateView(entity.getComponent<cro::Camera>());

    //need to delay by one update...
    //the prevents spurious input from previous states
    //once the animation is finished it sets the correct active group
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float)
    {
        m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Dummy);
        e.getComponent<cro::Callback>().active = false;
        m_uiScene.destroyEntity(e);
    };
}

void ClubhouseState::createMainMenu(cro::Entity parent, std::uint32_t mouseEnter, std::uint32_t mouseExit)
{
    auto menuEntity = m_uiScene.createEntity();
    menuEntity.addComponent<cro::Transform>();
    menuEntity.addComponent<cro::Callback>().setUserData<MenuData>();
    menuEntity.getComponent<cro::Callback>().getUserData<MenuData>().direction = MenuData::Out;
    menuEntity.getComponent<cro::Callback>().getUserData<MenuData>().currentTime = 1.f;
    menuEntity.getComponent<cro::Callback>().function = MenuCallback(ClubhouseContext(this));
    m_menuEntities[MenuID::Main] = menuEntity;
    parent.getComponent<cro::Transform>().addChild(menuEntity.getComponent<cro::Transform>());

    auto& menuTransform = menuEntity.getComponent<cro::Transform>();
    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/main_menu.spt", m_resources.textures);

    //just store these here to save reloading the same sprite sheet
    m_sprites[SpriteID::ButtonBanner] = spriteSheet.getSprite("banner_small");
    m_sprites[SpriteID::Cursor] = spriteSheet.getSprite("cursor");
    m_sprites[SpriteID::Flag] = spriteSheet.getSprite("flag");
    m_sprites[SpriteID::AddPlayer] = spriteSheet.getSprite("add_player");
    m_sprites[SpriteID::RemovePlayer] = spriteSheet.getSprite("remove_player");
    m_sprites[SpriteID::PrevMenu] = spriteSheet.getSprite("exit");
    m_sprites[SpriteID::NextMenu] = spriteSheet.getSprite("continue");
    m_sprites[SpriteID::ReadyUp] = spriteSheet.getSprite("ready_up");
    m_sprites[SpriteID::StartGame] = spriteSheet.getSprite("start_game");
    m_sprites[SpriteID::Connect] = spriteSheet.getSprite("connect");


    //cursor
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ -100.f, 0.f, 0.1f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("cursor");
    entity.addComponent<cro::SpriteAnimation>().play(0);
    auto cursorEnt = entity;

    mouseEnter = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::Text>().setFillColour(TextGoldColour);
            e.getComponent<cro::AudioEmitter>().play();
            entity.getComponent<cro::Transform>().setPosition(e.getComponent<cro::Transform>().getPosition() + glm::vec3(-20.f, -7.f, 0.f));
            entity.getComponent<cro::Transform>().setScale(glm::vec2(1.f));
        });
    
    spriteSheet.loadFromFile("assets/golf/sprites/clubhouse_menu.spt", m_resources.textures);

    //title
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("title");
    auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, 0.f });
    entity.addComponent<UIElement>().absolutePosition = { 0.f, 0.f };
    entity.getComponent<UIElement>().relativePosition = { 0.5f, 0.75f };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::TitleText;

    entity.getComponent<cro::Transform>().setScale({ 0.f, 0.f });
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function = TitleTextCallback();
    auto titleEnt = entity;
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ (bounds.width / 2.f) + 1.f, bounds.height });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::Flag];
    entity.addComponent<cro::SpriteAnimation>().play(0);
    titleEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());


    //text background
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({12.f, 10.f, 0.f});
    entity.getComponent<cro::Transform>().setScale({ 0.f, 1.f });
    entity.getComponent<cro::Transform>().addChild(cursorEnt.getComponent<cro::Transform>());
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("menu_background");
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    entity.getComponent<cro::Transform>().move(entity.getComponent<cro::Transform>().getOrigin());
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().setUserData<float>(-1.5f);
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float dt)
    {
        auto& currTime = e.getComponent<cro::Callback>().getUserData<float>();
        currTime = std::min(1.f, currTime + (dt * 2.f));

        float progress = std::max(0.f, currTime);
        float xScale = cro::Util::Easing::easeOutSine(progress);
        float yScale = 0.f;// std::cos((/*0.5f + */(0.5f * progress)) * cro::Util::Const::PI) * 0.1f;

        e.getComponent<cro::Transform>().setScale({ xScale, 1.f + yScale });

        if (progress == 1)
        {
            e.getComponent<cro::Transform>().setScale({ 1.f, 1.f });
            e.getComponent<cro::Callback>().active = false;

            m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Main);
        }
    };
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    auto boardEntity = entity;

    static constexpr float TextOffset = 28.f;
    static constexpr float LineSpacing = 10.f;
    glm::vec3 textPos = { TextOffset, 62.f, 0.1f };

    auto createButton = [&](const std::string& label)
    {
        entity = m_uiScene.createEntity();
        entity.addComponent<cro::Transform>().setPosition(textPos);
        entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
        entity.addComponent<cro::Drawable2D>();
        entity.addComponent<cro::Text>(font).setString(label);
        entity.getComponent<cro::Text>().setCharacterSize(UITextSize);
        entity.getComponent<cro::Text>().setFillColour(TextNormalColour);
        entity.addComponent<cro::UIInput>().area = cro::Text::getLocalBounds(entity);
        entity.getComponent<cro::UIInput>().setGroup(MenuID::Main);
        entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnter;
        entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = mouseExit;

        boardEntity.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
        textPos.y -= LineSpacing;

        return entity;
    };

    //billiards
    if (!m_tableData.empty())
    {
        entity = createButton("Billiards");
        entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
            m_uiScene.getSystem<cro::UISystem>()->addCallback([&, menuEntity](cro::Entity, const cro::ButtonEvent& evt) mutable
                {
                    if (activated(evt))
                    {
                        m_sharedData.hosting = true;
                        m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Dummy);
                        menuEntity.getComponent<cro::Callback>().getUserData<MenuData>().targetMenu = MenuID::PlayerSelect;
                        menuEntity.getComponent<cro::Callback>().active = true;

                        m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();
                    }
                });
    }
    else
    {
        entity = createButton("No Tables Found");
    }

    //arcade
    /*entity = createButton("Arcade (Soon!)");
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&](cro::Entity, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {

                }
            });*/

    //trophy shelf
    entity = createButton("Trophy Shelf");
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&](cro::Entity, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    requestStackPush(StateID::Trophy);
                }
            });

    //options
    entity = createButton("Options");
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&](cro::Entity, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    requestStackPush(StateID::Options);
                    m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();
                }
            });

    textPos.y -= LineSpacing; //remove this when adding back arcade

    //leave button
    entity = createButton("Leave");
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&](cro::Entity, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    quitLobby();

                    //reset this else we might put course data out of range
                    m_sharedData.courseIndex = 0;

                    requestStackClear();
                    requestStackPush(StateID::Menu);
                }
            });
}

void ClubhouseState::createAvatarMenu(cro::Entity parent, std::uint32_t mouseEnter, std::uint32_t mouseExit)
{
    auto menuEntity = m_uiScene.createEntity();
    menuEntity.addComponent<cro::Transform>().setScale(glm::vec2(0.f));
    menuEntity.addComponent<cro::Callback>().setUserData<MenuData>();
    menuEntity.getComponent<cro::Callback>().function = MenuCallback(ClubhouseContext(this));
    m_menuEntities[MenuID::PlayerSelect] = menuEntity;
    parent.getComponent<cro::Transform>().addChild(menuEntity.getComponent<cro::Transform>());

    auto& menuTransform = menuEntity.getComponent<cro::Transform>();
    menuTransform.setPosition(-m_menuPositions[MenuID::PlayerSelect]);

    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/player_menu.spt", m_resources.textures);

    //title
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.9f };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::TitleText;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("title");
    auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ std::floor(bounds.width / 2.f), std::floor(bounds.height / 2.f) });
    entity.addComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function = TitleTextCallback();
    menuTransform.addChild(entity.getComponent<cro::Transform>());


    //banner
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, BannerPosition, -0.1f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::ButtonBanner];
    auto spriteRect = entity.getComponent<cro::Sprite>().getTextureRect();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIBanner;
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, spriteRect](cro::Entity e, float)
    {
        auto rect = spriteRect;
        rect.width = static_cast<float>(GolfGame::getActiveTarget()->getSize().x) * m_viewScale.x;
        e.getComponent<cro::Sprite>().setTextureRect(rect);
        e.getComponent<cro::Callback>().active = false;
    };
    menuTransform.addChild(entity.getComponent<cro::Transform>());


    //cursor
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ -10000.f, 0.f, 0.1f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::Cursor];
    entity.addComponent<cro::SpriteAnimation>().play(0);
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    auto mouseEnterHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            auto bounds = e.getComponent<cro::Sprite>().getTextureRect();
            bounds.left = bounds.width + e.getComponent<cro::Callback>().getUserData<float>();
            e.getComponent<cro::Sprite>().setTextureRect(bounds);
            entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
        });
    auto mouseExitHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            auto bounds = e.getComponent<cro::Sprite>().getTextureRect();
            bounds.left = e.getComponent<cro::Callback>().getUserData<float>();
            e.getComponent<cro::Sprite>().setTextureRect(bounds);
        });
    auto arrowSelected = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            e.getComponent<cro::Sprite>().setColour(cro::Colour::White);
            e.getComponent<cro::Callback>().active = true;
            entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
        });
    auto arrowUnselected = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::AudioEmitter>().play();
            e.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        });

    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

    //player info
    auto editRoot = m_uiScene.createEntity();
    editRoot.addComponent<cro::Transform>();
    editRoot.addComponent<UIElement>().relativePosition = { 0.5f, 0.5f };
    editRoot.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    menuTransform.addChild(editRoot.getComponent<cro::Transform>());

    auto vsEnt = m_uiScene.createEntity();
    vsEnt.addComponent<cro::Transform>().setPosition({ 0.f, 0.f, 0.4f });
    vsEnt.addComponent<cro::Drawable2D>();
    vsEnt.addComponent<cro::Sprite>() = spriteSheet.getSprite("versus");
    bounds = vsEnt.getComponent<cro::Sprite>().getTextureBounds();
    vsEnt.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    editRoot.getComponent<cro::Transform>().addChild(vsEnt.getComponent<cro::Transform>());

    
    auto createPlayerEdit = [&, arrowSelected, arrowUnselected](glm::vec2 position, std::int32_t playerIndex)
    {
        auto editEnt = m_uiScene.createEntity();
        editEnt.addComponent<cro::Transform>().setPosition(glm::vec3(position, 0.1f));
        editEnt.addComponent<cro::Drawable2D>();
        editEnt.addComponent<cro::Sprite>() = spriteSheet.getSprite("billiards_panel");
        auto spriteBounds = editEnt.getComponent<cro::Sprite>().getTextureBounds();
        editEnt.getComponent<cro::Transform>().setOrigin({ spriteBounds.width / 2.f, std::floor(spriteBounds.height / 2.f) });

        auto textEnt = m_uiScene.createEntity();
        textEnt.addComponent<cro::Transform>().setPosition({ spriteBounds.width / 2.f, 48.f, 0.1f });
        textEnt.addComponent<cro::Drawable2D>();
        textEnt.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
        textEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
        textEnt.getComponent<cro::Text>().setString(m_sharedData.localConnectionData.playerData[playerIndex].name.substr(0, ConstVal::MaxStringChars));
        centreText(textEnt);

        bounds = cro::Text::getLocalBounds(textEnt);
        textEnt.addComponent<NameScroller>().maxDistance = bounds.width - NameWidth;
        bounds.width = NameWidth;
        textEnt.getComponent<cro::Drawable2D>().setCroppingArea(bounds);
        
        textEnt.addComponent<cro::Callback>().function =
            [&, spriteBounds](cro::Entity e, float)
        {
            if (m_textEdit.string != nullptr)
            {
                auto str = *m_textEdit.string;
                if (str.size() == 0/*< ConstVal::MaxNameChars*/)
                {
                    str += "_";
                }
                e.getComponent<cro::Text>().setString(str);

                centreText(e);
                bounds = cro::Text::getLocalBounds(e);
                bounds.left = (bounds.width - NameWidth) / 2.f;
                bounds.width = NameWidth;
                e.getComponent<cro::Drawable2D>().setCroppingArea(bounds);
            }
        };
        textEnt.getComponent<cro::Callback>().setUserData<const std::int32_t>(playerIndex);
        editEnt.getComponent<cro::Transform>().addChild(textEnt.getComponent<cro::Transform>());

        auto buttonEnt = m_uiScene.createEntity();
        buttonEnt.addComponent<cro::Transform>().setPosition({ spriteBounds.width / 2.f, 44.f, 0.1f });
        buttonEnt.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
        buttonEnt.addComponent<cro::Drawable2D>();
        buttonEnt.addComponent<cro::Sprite>() = spriteSheet.getSprite("name_highlight");
        buttonEnt.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        spriteBounds = buttonEnt.getComponent<cro::Sprite>().getTextureBounds();
        buttonEnt.getComponent<cro::Transform>().setOrigin({ spriteBounds.width / 2.f, spriteBounds.height / 2.f });
        buttonEnt.addComponent<cro::Callback>().function = HighlightAnimationCallback();
        buttonEnt.addComponent<cro::UIInput>().area = spriteBounds;
        buttonEnt.getComponent<cro::UIInput>().setGroup(MenuID::PlayerSelect);
        buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = arrowSelected;
        buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = arrowUnselected;
        buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
            m_uiScene.getSystem<cro::UISystem>()->addCallback(
                [&, textEnt, spriteBounds](cro::Entity, const cro::ButtonEvent& evt) mutable
                {
                    if (activated(evt))
                    {
                        auto& callback = textEnt.getComponent<cro::Callback>();
                        callback.active = !callback.active;
                        if (callback.active)
                        {
                            beginTextEdit(textEnt, &m_sharedData.localConnectionData.playerData[callback.getUserData<const std::int32_t>()].name, ConstVal::MaxStringChars);
                            m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();

                            textEnt.getComponent<NameScroller>().active = false;
                            auto pos = textEnt.getComponent<cro::Transform>().getPosition();
                            pos.x = (spriteBounds.width / 2.f) + 8.f; //not sure where +8 comes from...
                            textEnt.getComponent<cro::Transform>().setPosition(pos);

                            if (evt.type == SDL_CONTROLLERBUTTONUP)
                            {
                                requestStackPush(StateID::Keyboard);
                            }
                        }
                        else
                        {
                            applyTextEdit();
                            centreText(textEnt);
                            m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                        }
                    }
                });

        editEnt.getComponent<cro::Transform>().addChild(buttonEnt.getComponent<cro::Transform>());

        //disables the input if inactive (needs to be separate because the callback is already assigned *sigh*)
        if (playerIndex > 0)
        {
            auto callbackEnt = m_uiScene.createEntity();
            callbackEnt.addComponent<cro::Callback>().active = true;
            callbackEnt.getComponent<cro::Callback>().function =
                [editEnt, buttonEnt](cro::Entity, float) mutable
            {
                auto scale = editEnt.getComponent<cro::Transform>().getScale();
                if (scale.x * scale.y == 0)
                {
                    if (buttonEnt.getComponent<cro::UIInput>().getGroup() == MenuID::PlayerSelect)
                    {
                        buttonEnt.getComponent<cro::UIInput>().setGroup(MenuID::Inactive);
                    }
                }
                else
                {
                    if (buttonEnt.getComponent<cro::UIInput>().getGroup() == MenuID::Inactive)
                    {
                        buttonEnt.getComponent<cro::UIInput>().setGroup(MenuID::PlayerSelect);
                    }
                }
            };
        }        

        editRoot.getComponent<cro::Transform>().addChild(editEnt.getComponent<cro::Transform>());
        return editEnt;
    };

    static constexpr float BackgroundOffset = 110.f;
    struct EditCallbackData final
    {
        std::int32_t direction = 0;
        float currentTime = 1.f;
    };

    //player one
    entity = createPlayerEdit({ -BackgroundOffset, 0.f }, 0);
    entity.addComponent<cro::Callback>().setUserData<EditCallbackData>();
    entity.getComponent<cro::Callback>().function =
        [](cro::Entity e, float dt)
    {
        auto& [direction, currTime] = e.getComponent<cro::Callback>().getUserData<EditCallbackData>();
        float x = 0.f;

        if (direction == 0)
        {
            //move to centre
            currTime = std::max(0.f, currTime - dt);
            x = cro::Util::Easing::easeInBounce(currTime);

            if (currTime == 0)
            {
                direction = 1;
                e.getComponent<cro::Callback>().active = false;
            }
        }
        else
        {
            //move left
            currTime = std::min(1.f, currTime + dt);
            x = cro::Util::Easing::easeOutBounce(currTime);

            if (currTime == 1)
            {
                direction = 0;
                e.getComponent<cro::Callback>().active = false;
            }
        }
        e.getComponent<cro::Transform>().setPosition({ -BackgroundOffset * x, 0.f });
    };
    auto playerOne = entity;

    //player two
    entity = createPlayerEdit({ BackgroundOffset, 0.f }, 1);
    entity.addComponent<cro::Callback>().setUserData<EditCallbackData>();
    entity.getComponent<cro::Callback>().function =
        [vsEnt](cro::Entity e, float dt) mutable
    {
        auto& [direction, currTime] = e.getComponent<cro::Callback>().getUserData<EditCallbackData>();
        float ts = dt * 5.f;

        if (direction == 0)
        {
            //shrink
            currTime = std::max(0.f, currTime - ts);

            if (currTime == 0)
            {
                direction = 1;
                e.getComponent<cro::Callback>().active = false;

                //TODO disable the input controls
            }
        }
        else
        {
            //grow
            currTime = std::min(1.f, currTime + ts);

            if (currTime == 1)
            {
                direction = 0;
                e.getComponent<cro::Callback>().active = false;
            }
        }
        e.getComponent<cro::Transform>().setScale({ cro::Util::Easing::easeInOutCubic(currTime), 1.f });

        cro::Colour c(1.f, 1.f, 1.f, currTime);
        vsEnt.getComponent<cro::Sprite>().setColour(c);
    };
    auto playerTwo = entity;




    //back
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 20.f, MenuBottomBorder });
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevMenu];
    //fudge in here as storage for clamping sprite bounds
    entity.addComponent<cro::Callback>().setUserData<float>(m_sprites[SpriteID::PrevMenu].getTextureRect().left);
    entity.addComponent<cro::UIInput>().area = m_sprites[SpriteID::PrevMenu].getTextureBounds();
    entity.getComponent<cro::UIInput>().setGroup(MenuID::PlayerSelect);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnterHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = mouseExitHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&, menuEntity](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    applyTextEdit();

                    m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Dummy);
                    menuEntity.getComponent<cro::Callback>().getUserData<MenuData>().targetMenu = MenuID::Main;
                    menuEntity.getComponent<cro::Callback>().active = true;

                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    std::array<std::function<void()>, MaxGameIndices> gameCreateCallbacks = {};
    gameCreateCallbacks[0] = 
        [&, playerOne, playerTwo]() mutable
    {
        //add player
        if (m_sharedData.localConnectionData.playerCount < 2)
        {
            auto index = m_sharedData.localConnectionData.playerCount;

            if (m_sharedData.localConnectionData.playerData[index].name.empty())
            {
                m_sharedData.localConnectionData.playerData[index].name = RandomNames[cro::Util::Random::value(0u, RandomNames.size() - 1)];
            }
            m_sharedData.localConnectionData.playerCount++;

            playerOne.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 1;
            playerOne.getComponent<cro::Callback>().active = true;

            playerTwo.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 1;
            playerTwo.getComponent<cro::Callback>().active = true;
        }
        //set host
        m_sharedData.hosting = true;
    };
    gameCreateCallbacks[1] = 
        [&, playerOne, playerTwo]() mutable
    {
        //remove player if necessary
        if (m_sharedData.localConnectionData.playerCount > 1)
        {
            m_sharedData.localConnectionData.playerCount--;

            playerOne.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 0;
            playerOne.getComponent<cro::Callback>().active = true;

            playerTwo.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 0;
            playerTwo.getComponent<cro::Callback>().active = true;
        }

        //set not host
        m_sharedData.hosting = false;
    };
    gameCreateCallbacks[2] = 
        [&, playerOne, playerTwo]() mutable
    {
        //remove player if necessary
        if (m_sharedData.localConnectionData.playerCount > 1)
        {
            m_sharedData.localConnectionData.playerCount--;

            playerOne.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 0;
            playerOne.getComponent<cro::Callback>().active = true;

            playerTwo.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 0;
            playerTwo.getComponent<cro::Callback>().active = true;
        }

        //set host
        m_sharedData.hosting = true;
    };


    //we may be returning from a previous game in which case we need to set the correct menu layout
    //but not update  the playercount/host state because this is already set!
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, playerOne, playerTwo](cro::Entity e, float) mutable
    {
        if (m_sharedData.localConnectionData.playerCount == 1)
        {
            if (m_sharedData.hosting)
            {
                m_gameCreationIndex = 2;
            }
            else
            {
                m_gameCreationIndex = 1;
            }
            playerOne.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 0;
            playerOne.getComponent<cro::Callback>().active = true;

            playerTwo.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 0;
            playerTwo.getComponent<cro::Callback>().active = true;
        }
        else
        {
            m_gameCreationIndex = 0;

            playerOne.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 1;
            playerOne.getComponent<cro::Callback>().active = true;

            playerTwo.getComponent<cro::Callback>().getUserData<EditCallbackData>().direction = 1;
            playerTwo.getComponent<cro::Callback>().active = true;
        }

        e.getComponent<cro::Callback>().active = false;
        m_uiScene.destroyEntity(e);
    };

    spriteSheet.loadFromFile("assets/golf/sprites/billiards_ui.spt", m_resources.textures);

    //prev selection button
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { -50.f, MenuBottomBorder };
    entity.getComponent<UIElement>().relativePosition = { 0.5f, 0.f };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("arrow_left");
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<UIElement>().absolutePosition.x -= bounds.width / 2.f;
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::PlayerSelect);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = arrowSelected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = arrowUnselected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&, gameCreateCallbacks](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    applyTextEdit();
                    m_gameCreationIndex = (m_gameCreationIndex + (MaxGameIndices - 1)) % MaxGameIndices;
                    gameCreateCallbacks[m_gameCreationIndex]();
                    m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();
                }
            });
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    auto arrowEnt = entity;
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("arrow_fill_left");
    arrowEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //next selection button
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { 50.f, MenuBottomBorder };
    entity.getComponent<UIElement>().relativePosition = { 0.5f, 0.f };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("arrow_right");
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<UIElement>().absolutePosition.x -= bounds.width / 2.f;
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::PlayerSelect);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = arrowSelected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = arrowUnselected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&,gameCreateCallbacks](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    applyTextEdit();
                    m_gameCreationIndex = (m_gameCreationIndex + 1) % MaxGameIndices;
                    gameCreateCallbacks[m_gameCreationIndex]();                    
                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    arrowEnt = entity;
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("arrow_fill_right");
    arrowEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //status text    
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(TextNormalColour);
    entity.addComponent<UIElement>().absolutePosition = { 0.f, MenuBottomBorder + 12.f };
    entity.getComponent<UIElement>().relativePosition = { 0.5f, 0.f };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;

    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&](cro::Entity e, float)
    {
        if (m_sharedData.localConnectionData.playerCount == 1)
        {
            if (m_sharedData.hosting)
            {
                e.getComponent<cro::Text>().setString("Host Game");
            }
            else
            {
                e.getComponent<cro::Text>().setString("Join Game");
            }
        }
        else
        {
            e.getComponent<cro::Text>().setString("Local Game");
        }
        centreText(e);
    };
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    //continue
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { 0.f, MenuBottomBorder };
    entity.getComponent<UIElement>().relativePosition = { 0.98f, 0.f };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::NextMenu];
    //fudge in here as storage for clamping sprite bounds
    entity.addComponent<cro::Callback>().setUserData<float>(m_sprites[SpriteID::NextMenu].getTextureRect().left);
    bounds = m_sprites[SpriteID::NextMenu].getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::PlayerSelect);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnterHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = mouseExitHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&, menuEntity](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    applyTextEdit();
                    saveAvatars(m_sharedData);

                    m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();

                    if (m_sharedData.hosting)
                    {
                        if (!m_sharedData.clientConnection.connected)
                        {
                            m_sharedData.serverInstance.launch(2, Server::GameMode::Billiards);

                            //small delay for server to get ready
                            cro::Clock clock;
                            while (clock.elapsed().asMilliseconds() < 500) {}

                            m_matchMaking.createGame(2, Server::GameMode::Billiards);
                        }
                    }
                    else
                    {
                        m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Dummy);
                        menuEntity.getComponent<cro::Callback>().getUserData<MenuData>().targetMenu = MenuID::Join;
                        menuEntity.getComponent<cro::Callback>().active = true;

                        m_matchMaking.refreshLobbyList(Server::GameMode::Billiards);
                        updateLobbyList();
                    }

                    //kludgy way of temporarily disabling this button to prevent double clicks
                    auto defaultCallback = e.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp];
                    e.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] = 0;

                    auto tempEnt = m_uiScene.createEntity();
                    tempEnt.addComponent<cro::Callback>().active = true;
                    tempEnt.getComponent<cro::Callback>().setUserData<std::pair<std::uint32_t, float>>(defaultCallback, 0.f);
                    tempEnt.getComponent<cro::Callback>().function =
                        [&, e](cro::Entity t, float dt) mutable
                    {
                        auto& [cb, currTime] = t.getComponent<cro::Callback>().getUserData<std::pair<std::uint32_t, float>>();
                        currTime += dt;
                        if (currTime > 1)
                        {
                            e.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] = cb;
                            t.getComponent<cro::Callback>().active = false;
                            m_uiScene.destroyEntity(t);
                        }
                    };
                }
            });
    entity.getComponent<UIElement>().absolutePosition.x = -bounds.width;
    menuTransform.addChild(entity.getComponent<cro::Transform>());


    //create the table selection callbacks here as they are dynamically
    //added to the lobby menu based on whether or not we're currently hosting
    m_tableSelectCallbacks.prevTable = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [&](cro::Entity, const cro::ButtonEvent& evt)
        {
            if (activated(evt))
            {
                m_sharedData.courseIndex = (m_sharedData.courseIndex + (m_tableData.size() - 1)) % m_tableData.size();

                m_sharedData.mapDirectory = m_tableData[m_sharedData.courseIndex].name;
                auto data = serialiseString(m_sharedData.mapDirectory);
                m_sharedData.clientConnection.netClient.sendPacket(PacketID::MapInfo, data.data(), data.size(), net::NetFlag::Reliable, ConstVal::NetChannelStrings);

                m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();
            }
        });
    m_tableSelectCallbacks.nextTable = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [&](cro::Entity, const cro::ButtonEvent& evt)
        {
            if (activated(evt))
            {
                m_sharedData.courseIndex = (m_sharedData.courseIndex + 1) % m_tableData.size();

                m_sharedData.mapDirectory = m_tableData[m_sharedData.courseIndex].name;
                auto data = serialiseString(m_sharedData.mapDirectory);
                m_sharedData.clientConnection.netClient.sendPacket(PacketID::MapInfo, data.data(), data.size(), net::NetFlag::Reliable, ConstVal::NetChannelStrings);

                m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
            }
        });

    m_tableSelectCallbacks.mouseEnter = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Sprite>().setColour(cro::Colour::White);
            e.getComponent<cro::AudioEmitter>().play();
            e.getComponent<cro::Callback>().active = true;
        });
    m_tableSelectCallbacks.mouseExit = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        });

    m_tableSelectCallbacks.selectHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Sprite>().setColour(cro::Colour::White);
            e.getComponent<cro::AudioEmitter>().play();
        });
    m_tableSelectCallbacks.unselectHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        });

    m_tableSelectCallbacks.toggleFriendsOnly = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [&](cro::Entity, const cro::ButtonEvent& evt)
        {
            if (activated(evt))
            {
                m_matchMaking.setFriendsOnly(!m_matchMaking.getFriendsOnly());

                m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
            }
        });

    //and store these to use as button icons
    m_sprites[SpriteID::PrevCourseHighlight] = spriteSheet.getSprite("arrow_left");
    m_sprites[SpriteID::PrevCourse] = spriteSheet.getSprite("arrow_fill_left");
    m_sprites[SpriteID::NextCourseHighlight] = spriteSheet.getSprite("arrow_right");
    m_sprites[SpriteID::NextCourse] = spriteSheet.getSprite("arrow_fill_right");
}

void ClubhouseState::createJoinMenu(cro::Entity parent, std::uint32_t mouseEnter, std::uint32_t mouseExit)
{
    auto menuEntity = m_uiScene.createEntity();
    menuEntity.addComponent<cro::Transform>().setScale(glm::vec2(0.f));
    menuEntity.addComponent<cro::Callback>().setUserData<MenuData>();
    menuEntity.getComponent<cro::Callback>().function = MenuCallback(ClubhouseContext(this));
    m_menuEntities[MenuID::Join] = menuEntity;
    parent.getComponent<cro::Transform>().addChild(menuEntity.getComponent<cro::Transform>());

    auto& menuTransform = menuEntity.getComponent<cro::Transform>();
    menuTransform.setPosition(-m_menuPositions[MenuID::Join]);

    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/connect_menu.spt", m_resources.textures);

    //title
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.9f };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::TitleText;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("title");
    auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ std::floor(bounds.width / 2.f), std::floor(bounds.height / 2.f) });
    entity.addComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function = TitleTextCallback();
    menuTransform.addChild(entity.getComponent<cro::Transform>());


    //cursor
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::Cursor];
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    entity.addComponent<cro::SpriteAnimation>().play(0);
    menuTransform.addChild(entity.getComponent<cro::Transform>());
    auto cursorEnt = entity;

    mouseEnter = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            entity.getComponent<cro::Transform>().setPosition(e.getComponent<cro::Transform>().getPosition() + CursorOffset);
            entity.getComponent<cro::Sprite>().setColour(cro::Colour::White);
            entity.getComponent<cro::Transform>().setScale({ 1.f, 1.f });
        });

    mouseExit = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity) mutable
        {
            entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        });

    auto mouseEnterHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            auto bounds = e.getComponent<cro::Sprite>().getTextureRect();
            bounds.left += bounds.width;
            e.getComponent<cro::Sprite>().setTextureRect(bounds);
            entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
        });
    auto mouseExitHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            auto bounds = e.getComponent<cro::Sprite>().getTextureRect();
            bounds.left -= bounds.width;
            e.getComponent<cro::Sprite>().setTextureRect(bounds);
        });


    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

    //ip text
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>().setCroppingArea({ 0.f, 0.f, 200.f, -16.f });
    entity.addComponent<cro::Text>(font).setString(m_sharedData.targetIP);
    entity.getComponent<cro::Text>().setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(TextNormalColour);
    entity.addComponent<cro::Callback>().function =
        [&](cro::Entity e, float)
    {
        //add a cursor to the end of the string when active
        cro::String str = m_sharedData.targetIP;
        if (str.size() < ConstVal::MaxIPChars)
        {
            str += "_";
        }
        e.getComponent<cro::Text>().setString(str);
    };
    menuTransform.addChild(entity.getComponent<cro::Transform>());
    auto textEnt = entity;

    auto highlight = m_uiScene.createEntity();
    highlight.addComponent<cro::Transform>().setPosition({ 11.f, 16.f, 0.1f });
    highlight.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    highlight.addComponent<cro::Drawable2D>();
    highlight.addComponent<cro::Sprite>() = spriteSheet.getSprite("highlight");

    auto balls = m_uiScene.createEntity();
    balls.addComponent<cro::Transform>().setPosition({ 0.f, 0.f, 0.1f });
    balls.addComponent<cro::Drawable2D>();
    balls.addComponent<cro::Sprite>() = spriteSheet.getSprite("bounce");
    balls.addComponent<cro::SpriteAnimation>().play(0);
    bounds = balls.getComponent<cro::Sprite>().getTextureBounds();
    balls.getComponent<cro::Transform>().setOrigin({ std::floor(bounds.width / 2.f), 0.f });


    //box background
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.5f };
    entity.getComponent<UIElement>().depth = -0.01f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("background");
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [highlight, cursorEnt](cro::Entity) mutable
            { 
                highlight.getComponent<cro::Sprite>().setColour(cro::Colour::White);
                highlight.getComponent<cro::AudioEmitter>().play();
                cursorEnt.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
            });
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([highlight](cro::Entity) mutable { highlight.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent); });
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&, textEnt](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    auto& callback = textEnt.getComponent<cro::Callback>();
                    callback.active = !callback.active;
                    if (callback.active)
                    {
                        beginTextEdit(textEnt, &m_sharedData.targetIP, ConstVal::MaxIPChars);
                        m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();

                        if (evt.type == SDL_CONTROLLERBUTTONUP)
                        {
                            requestStackPush(StateID::Keyboard);
                        }
                    }
                    else
                    {
                        applyTextEdit();
                        m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                    }
                }
            });
    textEnt.getComponent<cro::Transform>().setPosition(entity.getComponent<cro::Transform>().getOrigin());
    textEnt.getComponent<cro::Transform>().move({ -60.f, -12.f, 0.1f });
    balls.getComponent<cro::Transform>().setPosition(entity.getComponent<cro::Transform>().getOrigin());
    balls.getComponent<cro::Transform>().move({ 0.f, 0.f, 0.1f });
    entity.getComponent<cro::Transform>().addChild(textEnt.getComponent<cro::Transform>());
    entity.getComponent<cro::Transform>().addChild(highlight.getComponent<cro::Transform>());
    entity.getComponent<cro::Transform>().addChild(balls.getComponent<cro::Transform>());
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    //banner
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, BannerPosition, -0.1f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::ButtonBanner];
    auto spriteRect = entity.getComponent<cro::Sprite>().getTextureRect();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIBanner;
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, spriteRect](cro::Entity e, float)
    {
        auto rect = spriteRect;
        rect.width = static_cast<float>(GolfGame::getActiveTarget()->getSize().x) * m_viewScale.x;
        e.getComponent<cro::Sprite>().setTextureRect(rect);
        e.getComponent<cro::Callback>().active = false;
    };
    menuTransform.addChild(entity.getComponent<cro::Transform>());


    //back
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { 20.f, MenuBottomBorder };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevMenu];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnterHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = mouseExitHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&, menuEntity](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    applyTextEdit();

                    m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Dummy);
                    menuEntity.getComponent<cro::Callback>().getUserData<MenuData>().targetMenu = MenuID::PlayerSelect;
                    menuEntity.getComponent<cro::Callback>().active = true;

                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    //join
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { -40.f, MenuBottomBorder };
    entity.getComponent<UIElement>().relativePosition = { 0.98f, 0.f };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::Connect];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnter;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    applyTextEdit(); //finish any pending changes

                    m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();


                    if (!m_sharedData.targetIP.empty() &&
                        !m_sharedData.clientConnection.connected)
                    {
                        m_matchMaking.joinGame(0);
                    }

                    auto defaultCallback = e.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown];
                    e.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] = 0;

                    auto tempEnt = m_uiScene.createEntity();
                    tempEnt.addComponent<cro::Callback>().active = true;
                    tempEnt.getComponent<cro::Callback>().setUserData<std::pair<std::uint32_t, float>>(defaultCallback, 0.f);
                    tempEnt.getComponent<cro::Callback>().function =
                        [&, e](cro::Entity t, float dt) mutable
                    {
                        auto& [cb, currTime] = t.getComponent<cro::Callback>().getUserData<std::pair<std::uint32_t, float>>();
                        currTime += dt;
                        if (currTime > 1)
                        {
                            e.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] = cb;
                            t.getComponent<cro::Callback>().active = false;
                            m_uiScene.destroyEntity(t);
                        }
                    };
                }
            });

    menuTransform.addChild(entity.getComponent<cro::Transform>());
}

void ClubhouseState::createBrowserMenu(cro::Entity parent, std::uint32_t mouseEnter, std::uint32_t mouseExit)
{
    auto menuEntity = m_uiScene.createEntity();
    menuEntity.addComponent<cro::Transform>().setScale(glm::vec2(0.f));
    menuEntity.addComponent<cro::Callback>().setUserData<MenuData>();
    menuEntity.getComponent<cro::Callback>().function = MenuCallback(ClubhouseContext(this));
    m_menuEntities[MenuID::Join] = menuEntity;
    parent.getComponent<cro::Transform>().addChild(menuEntity.getComponent<cro::Transform>());

    auto& menuTransform = menuEntity.getComponent<cro::Transform>();
    menuTransform.setPosition(-m_menuPositions[MenuID::Join]);

    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/lobby_browser.spt", m_resources.textures);


    //title
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.9f };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::TitleText;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("title");
    auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ std::floor(bounds.width / 2.f), std::floor(bounds.height / 2.f) });
    entity.addComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function = TitleTextCallback();
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    //background
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.55f };
    entity.getComponent<UIElement>().depth = -0.01f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("background");
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    menuTransform.addChild(entity.getComponent<cro::Transform>());
    m_lobbyPager.rootNode = entity;


    //banner
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, BannerPosition, -0.1f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::ButtonBanner];
    auto spriteRect = entity.getComponent<cro::Sprite>().getTextureRect();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIBanner;
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, spriteRect](cro::Entity e, float)
    {
        auto rect = spriteRect;
        rect.width = static_cast<float>(GolfGame::getActiveTarget()->getSize().x) * m_viewScale.x;
        e.getComponent<cro::Sprite>().setTextureRect(rect);
        e.getComponent<cro::Callback>().active = false;
    };
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    //cursor
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::Cursor];
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    entity.addComponent<cro::SpriteAnimation>().play(0);
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    mouseEnter = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            entity.getComponent<cro::Transform>().setPosition(e.getComponent<cro::Transform>().getPosition() + CursorOffset);
            entity.getComponent<cro::Transform>().setScale({ 1.f, 1.f });
            entity.getComponent<cro::Sprite>().setColour(cro::Colour::White);
        });

    mouseExit = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity) mutable
        {
            entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        });

    auto mouseEnterHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            auto bounds = e.getComponent<cro::Sprite>().getTextureRect();
            bounds.left += bounds.width;
            e.getComponent<cro::Sprite>().setTextureRect(bounds);
            entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
        });
    auto mouseExitHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            auto bounds = e.getComponent<cro::Sprite>().getTextureRect();
            bounds.left -= bounds.width;
            e.getComponent<cro::Sprite>().setTextureRect(bounds);
        });

    auto arrowSelected = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [&, entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            e.getComponent<cro::Sprite>().setColour(cro::Colour::White);
            e.getComponent<cro::Callback>().active = true;
            entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
        });
    auto arrowUnselected = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            e.getComponent<cro::AudioEmitter>().play();
            e.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        });

    auto lobbyActivated = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [&](cro::Entity e, const cro::ButtonEvent& evt)
        {
            if (activated(evt))
            {
                std::size_t idx = e.getComponent<cro::Callback>().getUserData<std::uint32_t>();
                idx += (LobbyPager::ItemsPerPage * m_lobbyPager.currentPage);

                if (idx < m_lobbyPager.lobbyIDs.size())
                {
                    //this will be reset next time the page is scrolled, and prevents double presses
                    e.getComponent<cro::UIInput>().enabled = false;

                    m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();

                    m_matchMaking.joinGame(m_lobbyPager.lobbyIDs[idx]);
                    m_sharedData.lobbyID = m_lobbyPager.lobbyIDs[idx];
                }
            }
        });

    //entry highlights
    glm::vec2 highlightPos(6.f, 161.f);
    for (auto i = 0u; i < LobbyPager::ItemsPerPage; ++i)
    {
        entity = m_uiScene.createEntity();
        entity.addComponent<cro::Transform>().setPosition(highlightPos);
        entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
        entity.addComponent<cro::Drawable2D>();
        entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("lobby_highlight");
        entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
        entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
        entity.getComponent<cro::Transform>().move(entity.getComponent<cro::Transform>().getOrigin());

        entity.addComponent<cro::Callback>().setUserData<std::uint32_t>(i); //used by button activated callback
        entity.getComponent<cro::Callback>().function = HighlightAnimationCallback();

        entity.addComponent<cro::UIInput>().area = bounds;
        entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
        entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = arrowSelected;
        entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = arrowUnselected;
        entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] = lobbyActivated;

        m_lobbyPager.rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
        m_lobbyPager.slots.push_back(entity);
        highlightPos.y -= entity.getComponent<cro::Sprite>().getTextureBounds().height;
    }

    auto updateActiveSlots = [&]()
    {
        auto start = m_lobbyPager.currentPage * LobbyPager::ItemsPerPage;
        auto end = start + LobbyPager::ItemsPerPage;

        for (auto i = start; i < end; ++i)
        {
            m_lobbyPager.slots[i % LobbyPager::ItemsPerPage].getComponent<cro::UIInput>().enabled = (i < m_lobbyPager.lobbyIDs.size());
        }
    };

    //button left
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 13.f, 5.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("button_left");
    m_lobbyPager.rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    m_lobbyPager.buttonLeft[0] = entity;

    //highlight left
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 11.f, 3.f });
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("highlight_left");
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    entity.getComponent<cro::Transform>().move({ bounds.width / 2.f, bounds.height / 2.f });
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = arrowSelected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = arrowUnselected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&, updateActiveSlots](cro::Entity e, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    if (m_lobbyPager.pages.size() > 1)
                    {
                        m_lobbyPager.pages[m_lobbyPager.currentPage].getComponent<cro::Transform>().setScale(glm::vec2(0.f));
                        m_lobbyPager.currentPage = (m_lobbyPager.currentPage + (m_lobbyPager.pages.size() - 1)) % m_lobbyPager.pages.size();
                        m_lobbyPager.pages[m_lobbyPager.currentPage].getComponent<cro::Transform>().setScale(glm::vec2(1.f));

                        //only enable item slots as available on the new page
                        updateActiveSlots();

                        m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                    }
                }
            });
    m_lobbyPager.rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    m_lobbyPager.buttonLeft[1] = entity;


    //friends overlay
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { 200.f, 10.f };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("friends_highlight");
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ std::floor(bounds.width / 2.f), std::floor(bounds.height / 2.f) });
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = arrowSelected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = arrowUnselected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&, menuEntity](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    Social::findFriends();
                    m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();
                }
            });
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    m_lobbyPager.rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //button right
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 382.f, 5.f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("button_right");
    m_lobbyPager.rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    m_lobbyPager.buttonRight[0] = entity;

    //highlight right
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 380.f, 3.f });
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("highlight_right");
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    entity.getComponent<cro::Transform>().move({ bounds.width / 2.f, bounds.height / 2.f });
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = arrowSelected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = arrowUnselected;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&, updateActiveSlots](cro::Entity e, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    if (m_lobbyPager.pages.size() > 1)
                    {
                        m_lobbyPager.pages[m_lobbyPager.currentPage].getComponent<cro::Transform>().setScale(glm::vec2(0.f));
                        m_lobbyPager.currentPage = (m_lobbyPager.currentPage + 1) % m_lobbyPager.pages.size();
                        m_lobbyPager.pages[m_lobbyPager.currentPage].getComponent<cro::Transform>().setScale(glm::vec2(1.f));

                        updateActiveSlots();

                        m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();
                    }
                }
            });
    m_lobbyPager.rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
    m_lobbyPager.buttonRight[1] = entity;

    //back
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { 20.f, MenuBottomBorder };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevMenu];
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnterHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = mouseExitHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&, menuEntity](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Dummy);
                    menuEntity.getComponent<cro::Callback>().getUserData<MenuData>().targetMenu = MenuID::PlayerSelect;
                    menuEntity.getComponent<cro::Callback>().active = true;

                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    menuTransform.addChild(entity.getComponent<cro::Transform>());


    //refresh
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setOrigin({ 0.f, -2.f });
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { -20.f, MenuBottomBorder };
    entity.getComponent<UIElement>().relativePosition = { 0.98f, 0.f };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("refresh");
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Join);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnter;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = mouseExit;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&](cro::Entity e, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();
                    m_matchMaking.refreshLobbyList(Server::GameMode::Billiards);
                    updateLobbyList(); //clears existing display until message comes in
                }
            });

    menuTransform.addChild(entity.getComponent<cro::Transform>());


    updateLobbyList();
}

void ClubhouseState::createLobbyMenu(cro::Entity parent, std::uint32_t mouseEnter, std::uint32_t mouseExit)
{
    auto menuEntity = m_uiScene.createEntity();
    menuEntity.addComponent<cro::Transform>().setScale(glm::vec2(0.f));
    menuEntity.addComponent<cro::Callback>().setUserData<MenuData>();
    menuEntity.getComponent<cro::Callback>().function = MenuCallback(ClubhouseContext(this));
    m_menuEntities[MenuID::Lobby] = menuEntity;
    parent.getComponent<cro::Transform>().addChild(menuEntity.getComponent<cro::Transform>());

    auto& menuTransform = menuEntity.getComponent<cro::Transform>();
    menuTransform.setPosition(-m_menuPositions[MenuID::Lobby]);

    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

    cro::SpriteSheet spriteSheet;
    spriteSheet.loadFromFile("assets/golf/sprites/lobby_menu.spt", m_resources.textures);
    m_sprites[SpriteID::ReadyStatus] = spriteSheet.getSprite("ready_status");
    m_sprites[SpriteID::LobbyCheckbox] = spriteSheet.getSprite("checkbox");
    m_sprites[SpriteID::LobbyCheckboxHighlight] = spriteSheet.getSprite("checkbox_highlight");

    //title
    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.9f };
    entity.getComponent<UIElement>().depth = 0.2f;
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::TitleText;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("title");
    auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
    entity.addComponent<cro::Callback>().setUserData<float>(0.f);
    entity.getComponent<cro::Callback>().function = TitleTextCallback();
    menuTransform.addChild(entity.getComponent<cro::Transform>());


    //player name node. Children are updated by updateLobbyAvatars()
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = spriteSheet.getSprite("versus");
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::Transform>().setOrigin({ std::floor(bounds.width / 2.f), std::floor(bounds.height- (bounds.height / 8.f))});
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::LobbyList | CommandID::Menu::UIElement;
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.75f };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::Callback>().setUserData<std::vector<cro::Entity>>(); //use this to store child nodes updated with updateLobbyAvatars()
    auto vsEnt = entity;
    menuTransform.addChild(entity.getComponent<cro::Transform>());


    //table preview
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 28.f, 20.f, 0.1f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>(m_tableTexture.getTexture());
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseDesc;
    vsEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //table/ball skins are selectable per-client as these may be colour-blind
    //specific for example. The host shouldn't be able to force a colour scheme
    //on someone who is impaired.
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({38.f, 36.f, 0.1f});
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevCourseHighlight];
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseHoles;
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
    entity.getComponent<cro::UIInput>().setSelectionIndex(3); //this is a bit arbitrary to read - but essentially makes the button selection order more intuitive to the player
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = m_tableSelectCallbacks.mouseEnter;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = m_tableSelectCallbacks.mouseExit;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&](cro::Entity e, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    auto& table = m_tableData[m_tableIndex];
                    table.ballSkinIndex = (table.ballSkinIndex + static_cast<std::int32_t>(table.ballSkins.size() - 1)) % table.ballSkins.size();
                    updateBallTexture();

                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    vsEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());


    auto arrowEnt = m_uiScene.createEntity();
    arrowEnt.addComponent<cro::Transform>();
    arrowEnt.addComponent<cro::Drawable2D>();
    arrowEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevCourse];
    entity.getComponent<cro::Transform>().addChild(arrowEnt.getComponent<cro::Transform>());


    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 104.f, 36.f, 0.1f });
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::NextCourseHighlight];
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseHoles;
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
    entity.getComponent<cro::UIInput>().setSelectionIndex(4);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = m_tableSelectCallbacks.mouseEnter;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = m_tableSelectCallbacks.mouseExit;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&](cro::Entity e, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    auto& table = m_tableData[m_tableIndex];
                    table.ballSkinIndex = (table.ballSkinIndex + 1) % table.ballSkins.size();
                    updateBallTexture();

                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    vsEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    arrowEnt = m_uiScene.createEntity();
    arrowEnt.addComponent<cro::Transform>();
    arrowEnt.addComponent<cro::Drawable2D>();
    arrowEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::NextCourse];
    entity.getComponent<cro::Transform>().addChild(arrowEnt.getComponent<cro::Transform>());





    //selects table texture
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 38.f, 86.f, 0.1f });
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevCourseHighlight];
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::ScoreType;
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
    entity.getComponent<cro::UIInput>().setSelectionIndex(1);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = m_tableSelectCallbacks.mouseEnter;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = m_tableSelectCallbacks.mouseExit;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&](cro::Entity e, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    auto& table = m_tableData[m_tableIndex];
                    table.tableSkinIndex = (table.tableSkinIndex + static_cast<std::int32_t>(table.tableSkins.size() - 1)) % table.tableSkins.size();
                    table.previewModel.getComponent<cro::Model>().setMaterialProperty(0, "u_diffuseMap",
                        cro::TextureID(m_resources.textures.get(table.tableSkins[table.tableSkinIndex])));

                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    vsEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    arrowEnt = m_uiScene.createEntity();
    arrowEnt.addComponent<cro::Transform>();
    arrowEnt.addComponent<cro::Drawable2D>();
    arrowEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevCourse];
    entity.getComponent<cro::Transform>().addChild(arrowEnt.getComponent<cro::Transform>());


    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 248.f, 86.f, 0.1f });
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::NextCourseHighlight];
    entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    entity.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::ScoreType;
    bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.addComponent<cro::UIInput>().area = bounds;
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
    entity.getComponent<cro::UIInput>().setSelectionIndex(2);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = m_tableSelectCallbacks.mouseEnter;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = m_tableSelectCallbacks.mouseExit;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback(
            [&](cro::Entity e, const cro::ButtonEvent& evt)
            {
                if (activated(evt))
                {
                    auto& table = m_tableData[m_tableIndex];
                    table.tableSkinIndex = (table.tableSkinIndex + 1) % table.tableSkins.size();
                    table.previewModel.getComponent<cro::Model>().setMaterialProperty(0, "u_diffuseMap",
                        cro::TextureID(m_resources.textures.get(table.tableSkins[table.tableSkinIndex])));

                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    vsEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    arrowEnt = m_uiScene.createEntity();
    arrowEnt.addComponent<cro::Transform>();
    arrowEnt.addComponent<cro::Drawable2D>();
    arrowEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::NextCourse];
    entity.getComponent<cro::Transform>().addChild(arrowEnt.getComponent<cro::Transform>());









    //banner
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ 0.f, BannerPosition, -0.1f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::ButtonBanner];
    auto spriteRect = entity.getComponent<cro::Sprite>().getTextureRect();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIBanner;
    entity.addComponent<cro::Callback>().active = true;
    entity.getComponent<cro::Callback>().function =
        [&, spriteRect](cro::Entity e, float)
    {
        auto rect = spriteRect;
        rect.width = static_cast<float>(GolfGame::getActiveTarget()->getSize().x) * m_viewScale.x;
        e.getComponent<cro::Sprite>().setTextureRect(rect);
        e.getComponent<cro::Callback>().active = false;
    };
    auto bannerEnt = entity;
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    //table type
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseTitle | CommandID::Menu::UIElement;
    entity.addComponent<UIElement>().relativePosition = { 0.5f, 0.f };
    entity.getComponent<UIElement>().absolutePosition = { 0.f, 15.f };
    entity.getComponent<UIElement>().depth = 0.1f;
    entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
    entity.getComponent<cro::Text>().setFillColour(TextNormalColour);
    bannerEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    //cursor
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>().setPosition({ -10000.f, 0.f, 0.1f });
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::Cursor];
    entity.addComponent<cro::SpriteAnimation>().play(0);
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    mouseEnter = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            entity.getComponent<cro::Transform>().setPosition(e.getComponent<cro::Transform>().getPosition() + CursorOffset);
            entity.getComponent<cro::Sprite>().setColour(cro::Colour::White);
            entity.getComponent<cro::Transform>().setScale({ 1.f, 1.f });
        });

    mouseExit = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity) mutable
        {
            entity.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
        });

    auto mouseEnterHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [entity](cro::Entity e) mutable
        {
            e.getComponent<cro::AudioEmitter>().play();
            auto bounds = e.getComponent<cro::Sprite>().getTextureRect();
            bounds.left = bounds.width + e.getComponent<cro::Callback>().getUserData<float>();
            e.getComponent<cro::Sprite>().setTextureRect(bounds);
            entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
        });
    auto mouseExitHighlight = m_uiScene.getSystem<cro::UISystem>()->addCallback(
        [](cro::Entity e)
        {
            auto bounds = e.getComponent<cro::Sprite>().getTextureRect();
            bounds.left = e.getComponent<cro::Callback>().getUserData<float>();
            e.getComponent<cro::Sprite>().setTextureRect(bounds);
        });

    //back
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { 20.f, MenuBottomBorder };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement;
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevMenu];
    entity.addComponent<cro::Callback>().setUserData<float>(m_sprites[SpriteID::PrevMenu].getTextureRect().left);
    entity.addComponent<cro::UIInput>().area = entity.getComponent<cro::Sprite>().getTextureBounds();
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
    entity.getComponent<cro::UIInput>().setSelectionIndex(5);
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnterHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = mouseExitHighlight;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    quitLobby();
                    m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                }
            });
    menuTransform.addChild(entity.getComponent<cro::Transform>());

    //start
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<UIElement>().absolutePosition = { -16.f, MenuBottomBorder };
    entity.getComponent<UIElement>().relativePosition = { 0.98f, 0.f };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::ReadyButton;
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::ReadyUp]; //which sprite is set by sending a message to this ent when we know if we're hosting or joining
    entity.addComponent<cro::UIInput>().area = m_sprites[SpriteID::ReadyUp].getTextureBounds();
    entity.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
    entity.getComponent<cro::UIInput>().setSelectionIndex(8); //table selection will be assigned 6/7 if hosting
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = mouseEnter;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = mouseExit;
    entity.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonDown] =
        m_uiScene.getSystem<cro::UISystem>()->addCallback([&](cro::Entity, const cro::ButtonEvent& evt) mutable
            {
                if (activated(evt))
                {
                    if (m_sharedData.hosting)
                    {
                        //check all members ready
                        bool ready = (m_sharedData.localConnectionData.playerCount == 2 || m_sharedData.connectionData[1].playerCount == 1);
                        for (auto i = 0u; i < ConstVal::MaxClients; ++i)
                        {
                            if (m_sharedData.connectionData[i].playerCount != 0
                                && !m_readyState[i])
                            {
                                ready = false;
                                break;
                            }
                        }

                        if (ready && m_sharedData.clientConnection.connected
                            && m_sharedData.serverInstance.running()) //not running if we're not hosting :)
                        {
                            m_sharedData.clientConnection.netClient.sendPacket(PacketID::RequestGameStart, std::uint8_t(sv::StateID::Billiards), net::NetFlag::Reliable, ConstVal::NetChannelReliable);
                        }
                    }
                    else
                    {
                        //toggle readyness
                        std::uint8_t ready = m_readyState[m_sharedData.clientConnection.connectionID] ? 0 : 1;
                        m_sharedData.clientConnection.netClient.sendPacket(PacketID::LobbyReady, std::uint16_t(m_sharedData.clientConnection.connectionID << 8 | ready),
                            net::NetFlag::Reliable, ConstVal::NetChannelReliable);

                        if (ready)
                        {
                            m_audioEnts[AudioID::Accept].getComponent<cro::AudioEmitter>().play();
                        }
                        else
                        {
                            m_audioEnts[AudioID::Back].getComponent<cro::AudioEmitter>().play();
                        }
                    }
                }
            });
    menuTransform.addChild(entity.getComponent<cro::Transform>());

#ifndef USE_GNS
    //server info message
    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<UIElement>().absolutePosition = { 10.f, 0.f };
    entity.getComponent<UIElement>().relativePosition = { 0.f, 0.98f };
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::ServerInfo;
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Text>(m_sharedData.sharedResources->fonts.get(FontID::Info)).setString("Connected to");
    entity.getComponent<cro::Text>().setCharacterSize(InfoTextSize);
    entity.getComponent<cro::Text>().setFillColour(cro::Colour::White);
    entity.getComponent<cro::Text>().setShadowColour(cro::Colour(std::uint8_t(110), 179, 157));
    entity.getComponent<cro::Text>().setShadowOffset({ 1.f, -1.f });
    menuTransform.addChild(entity.getComponent<cro::Transform>());
#endif
}

void ClubhouseState::updateLobbyData(const net::NetEvent& evt)
{
    ConnectionData cd;
    if (cd.deserialise(evt.packet))
    {
        m_sharedData.connectionData[cd.connectionID] = cd;
    }

    if (m_sharedData.hosting)
    {
        m_matchMaking.setGamePlayerCount(1);
    }

    updateLobbyAvatars();
}

void ClubhouseState::updateLobbyList()
{
#ifdef USE_GNS
    for (auto& e : m_lobbyPager.pages)
    {
        m_uiScene.destroyEntity(e);
    }

    m_lobbyPager.pages.clear();
    m_lobbyPager.lobbyIDs.clear();

    auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);
    const auto& lobbyData = m_matchMaking.getLobbies();
    if (lobbyData.empty())
    {
        //no lobbies found :(
        auto entity = m_uiScene.createEntity();
        entity.addComponent<cro::Transform>().setPosition(LobbyTextRootPosition);
        entity.addComponent<cro::Drawable2D>();
        entity.addComponent<cro::Text>(font).setString(" No Games Found.");
        entity.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
        entity.getComponent<cro::Text>().setCharacterSize(UITextSize);

        m_lobbyPager.rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
        m_lobbyPager.pages.push_back(entity);

        for (auto e : m_lobbyPager.slots)
        {
            e.getComponent<cro::UIInput>().enabled = false;
        }
    }
    else
    {
        auto pageCount = (lobbyData.size() / LobbyPager::ItemsPerPage) + 1;
        for (auto i = 0u; i < pageCount; ++i)
        {
            cro::String pageString;

            const auto startIndex = i * LobbyPager::ItemsPerPage;
            const auto endIndex = std::min(lobbyData.size(), startIndex + LobbyPager::ItemsPerPage);
            for (auto j = startIndex; j < endIndex; ++j)
            {
                std::stringstream ss;
                ss << " " << lobbyData[j].clientCount << "  " << std::setw(2) << std::setfill('0') << lobbyData[j].playerCount << " - ";
                pageString += ss.str();
                pageString += lobbyData[j].title + "\n";

                m_lobbyPager.lobbyIDs.push_back(lobbyData[j].ID);
            }

            auto entity = m_uiScene.createEntity();
            entity.addComponent<cro::Transform>().setPosition(LobbyTextRootPosition);
            entity.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
            entity.addComponent<cro::Drawable2D>();
            entity.addComponent<cro::Text>(font).setString(pageString);
            entity.getComponent<cro::Text>().setFillColour(LeaderboardTextDark);
            entity.getComponent<cro::Text>().setCharacterSize(UITextSize);
            entity.getComponent<cro::Text>().setVerticalSpacing(LeaderboardTextSpacing);

            m_lobbyPager.rootNode.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
            m_lobbyPager.pages.push_back(entity);
        }
        m_lobbyPager.pages[0].getComponent<cro::Transform>().setScale(glm::vec2(1.f));
        m_lobbyPager.currentPage = std::min(m_lobbyPager.currentPage, m_lobbyPager.pages.size() - 1);

        //enable slot highlights for current page
        auto start = m_lobbyPager.currentPage * LobbyPager::ItemsPerPage;
        auto end = start + LobbyPager::ItemsPerPage;

        for (auto i = start; i < end; ++i)
        {
            m_lobbyPager.slots[i % LobbyPager::ItemsPerPage].getComponent<cro::UIInput>().enabled = (i < m_lobbyPager.lobbyIDs.size());
        }
    }

    //hide or show buttons
    if (m_lobbyPager.pages.size() > 1)
    {
        m_lobbyPager.buttonLeft[0].getComponent<cro::Transform>().setScale({ 1.f, 1.f });
        m_lobbyPager.buttonLeft[1].getComponent<cro::Transform>().setScale({ 1.f, 1.f });
        m_lobbyPager.buttonLeft[1].getComponent<cro::UIInput>().enabled = true;

        m_lobbyPager.buttonRight[0].getComponent<cro::Transform>().setScale({ 1.f, 1.f });
        m_lobbyPager.buttonRight[1].getComponent<cro::Transform>().setScale({ 1.f, 1.f });
        m_lobbyPager.buttonRight[1].getComponent<cro::UIInput>().enabled = true;

        for (auto page : m_lobbyPager.pages)
        {
            page.getComponent<cro::Transform>().setScale(glm::vec2(0.f));
        }
        m_lobbyPager.pages[m_lobbyPager.currentPage].getComponent<cro::Transform>().setScale(glm::vec2(1.f));
    }
    else
    {
        m_lobbyPager.buttonLeft[0].getComponent<cro::Transform>().setScale({ 0.f, 1.f });
        m_lobbyPager.buttonLeft[1].getComponent<cro::Transform>().setScale({ 0.f, 1.f });
        m_lobbyPager.buttonLeft[1].getComponent<cro::UIInput>().enabled = false;

        m_lobbyPager.buttonRight[0].getComponent<cro::Transform>().setScale({ 0.f, 1.f });
        m_lobbyPager.buttonRight[1].getComponent<cro::Transform>().setScale({ 0.f, 1.f });
        m_lobbyPager.buttonRight[1].getComponent<cro::UIInput>().enabled = false;
    }
#endif
}

void ClubhouseState::quitLobby()
{
    m_sharedData.clientConnection.connected = false;
    m_sharedData.clientConnection.connectionID = 4;
    m_sharedData.clientConnection.ready = false;
    m_sharedData.clientConnection.netClient.disconnect();

    m_matchMaking.leaveGame();

    if (m_sharedData.hosting)
    {
        m_sharedData.serverInstance.stop();
        m_sharedData.hosting = false;

        for (auto& cd : m_sharedData.connectionData)
        {
            cd.playerCount = 0;
        }
    }

    m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(MenuID::Dummy);
    m_menuEntities[m_currentMenu].getComponent<cro::Callback>().getUserData<MenuData>().targetMenu = MenuID::Main;
    m_menuEntities[m_currentMenu].getComponent<cro::Callback>().active = true;

    //delete the course selection entities as they'll be re-created as needed
    cro::Command cmd;
    cmd.targetFlags = CommandID::Menu::CourseSelect;
    cmd.action =
        [&](cro::Entity b, float)
    {
        m_uiScene.destroyEntity(b);
    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);

    Social::setStatus(Social::InfoID::Menu, { "Clubhouse" });
    Social::setGroup(0);
}

void ClubhouseState::beginTextEdit(cro::Entity stringEnt, cro::String* dst, std::size_t maxChars)
{
    *dst = dst->substr(0, maxChars);

    stringEnt.getComponent<cro::Text>().setFillColour(TextEditColour);
    m_textEdit.string = dst;
    m_textEdit.entity = stringEnt;
    m_textEdit.maxLen = maxChars;

    //block input to menu
    m_prevMenu = m_currentMenu;
    m_currentMenu = MenuID::Dummy;
    m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(m_currentMenu);

    SDL_StartTextInput();
}

void ClubhouseState::handleTextEdit(const cro::Event& evt)
{
    if (!m_textEdit.string)
    {
        return;
    }

    if (evt.type == SDL_KEYDOWN)
    {
        switch (evt.key.keysym.sym)
        {
        default: break;
        case SDLK_BACKSPACE:
            if (!m_textEdit.string->empty())
            {
                m_textEdit.string->erase(m_textEdit.string->size() - 1);
            }
            break;
            //case SDLK_RETURN:
            //case SDLK_RETURN2:
                //applyTextEdit();
                //return;
        }

    }
    else if (evt.type == SDL_TEXTINPUT)
    {
        if (m_textEdit.string->size() < ConstVal::MaxStringChars
            && m_textEdit.string->size() < m_textEdit.maxLen)
        {
            auto codePoints = cro::Util::String::getCodepoints(evt.text.text);
            *m_textEdit.string += cro::String::fromUtf32(codePoints.begin(), codePoints.end());
        }
    }
}

bool ClubhouseState::applyTextEdit()
{
    if (m_textEdit.string && m_textEdit.entity.isValid())
    {
        if (m_textEdit.string->empty())
        {
            *m_textEdit.string = "INVALID";
        }

        m_textEdit.entity.getComponent<cro::Text>().setFillColour(TextNormalColour);
        m_textEdit.entity.getComponent<cro::Text>().setString(*m_textEdit.string);
        m_textEdit.entity.getComponent<cro::Callback>().active = false;


        auto& scroller = m_textEdit.entity.getComponent<NameScroller>();
        scroller.active = true;
        scroller.maxDistance = cro::Text::getLocalBounds(m_textEdit.entity).width - NameWidth;
        scroller.basePosition = (m_textEdit.entity.getComponent<cro::Transform>().getPosition().x) + (scroller.maxDistance / 2.f);



        //send this as a command to delay it by a frame - doesn't matter who receives it :)
        cro::Command cmd;
        cmd.targetFlags = CommandID::Menu::RootNode;
        cmd.action = [&](cro::Entity, float)
        {
            //commandception
            cro::Command cmd2;
            cmd2.targetFlags = CommandID::Menu::RootNode;
            cmd2.action = [&](cro::Entity, float)
            {
                m_currentMenu = m_prevMenu;
                m_uiScene.getSystem<cro::UISystem>()->setActiveGroup(m_currentMenu);
            };
            m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd2);
        };
        m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
        SDL_StopTextInput();
        m_textEdit = {};
        return true;
    }
    m_textEdit = {};
    return false;
}

void ClubhouseState::addTableSelectButtons()
{
    auto buttonEnt = m_uiScene.createEntity();
    buttonEnt.addComponent<cro::Transform>();
    buttonEnt.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    buttonEnt.addComponent<cro::Drawable2D>();
    buttonEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevCourseHighlight];
    buttonEnt.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    buttonEnt.addComponent<cro::SpriteAnimation>();
    buttonEnt.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::CourseSelect;
    buttonEnt.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    buttonEnt.addComponent<UIElement>().absolutePosition = { -50.f, MenuBottomBorder };
    buttonEnt.getComponent<UIElement>().relativePosition = { 0.5f, 0.f };
    buttonEnt.getComponent<UIElement>().depth = 0.01f;
    auto bounds = buttonEnt.getComponent<cro::Sprite>().getTextureBounds();
    buttonEnt.addComponent<cro::UIInput>().area = bounds;
    buttonEnt.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
    buttonEnt.getComponent<cro::UIInput>().setSelectionIndex(6);
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = m_tableSelectCallbacks.mouseEnter;
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = m_tableSelectCallbacks.mouseExit;
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] = m_tableSelectCallbacks.prevTable;

    buttonEnt.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, 0.f });

    m_menuEntities[MenuID::Lobby].getComponent<cro::Transform>().addChild(buttonEnt.getComponent<cro::Transform>());

    auto entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::PrevCourse];
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseSelect;
    buttonEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());


    buttonEnt = m_uiScene.createEntity();
    buttonEnt.addComponent<cro::Transform>();
    buttonEnt.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
    buttonEnt.addComponent<cro::Drawable2D>();
    buttonEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::NextCourseHighlight];
    buttonEnt.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
    buttonEnt.addComponent<cro::SpriteAnimation>();
    buttonEnt.addComponent<cro::CommandTarget>().ID = CommandID::Menu::UIElement | CommandID::Menu::CourseSelect;
    buttonEnt.addComponent<cro::Callback>().function = HighlightAnimationCallback();
    buttonEnt.addComponent<UIElement>().absolutePosition = { 50.f, MenuBottomBorder };
    buttonEnt.getComponent<UIElement>().relativePosition = { 0.5f, 0.f };
    buttonEnt.getComponent<UIElement>().depth = 0.01f;
    bounds = buttonEnt.getComponent<cro::Sprite>().getTextureBounds();
    buttonEnt.addComponent<cro::UIInput>().area = bounds;
    buttonEnt.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
    buttonEnt.getComponent<cro::UIInput>().setSelectionIndex(7);
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = m_tableSelectCallbacks.mouseEnter;
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = m_tableSelectCallbacks.mouseExit;
    buttonEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] = m_tableSelectCallbacks.nextTable;

    buttonEnt.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, 0.f });

    m_menuEntities[MenuID::Lobby].getComponent<cro::Transform>().addChild(buttonEnt.getComponent<cro::Transform>());

    entity = m_uiScene.createEntity();
    entity.addComponent<cro::Transform>();
    entity.addComponent<cro::Drawable2D>();
    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::NextCourse];
    entity.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseSelect;
    buttonEnt.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());

    if (Social::isAvailable()
        && m_sharedData.localConnectionData.playerCount == 1)
    {
        cro::Command cmd;
        cmd.targetFlags = CommandID::Menu::LobbyList;
        cmd.action = [&](cro::Entity e, float)
        {

            auto& font = m_sharedData.sharedResources->fonts.get(FontID::Info);

            //friends only lobby
            auto checkboxEnt = m_uiScene.createEntity();
            checkboxEnt.addComponent<cro::Transform>().setPosition({189.f, 21.f, 0.1f});
            checkboxEnt.addComponent<cro::Drawable2D>();
            checkboxEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::LobbyCheckbox];
            checkboxEnt.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseSelect;

            bounds = m_sprites[SpriteID::LobbyCheckbox].getTextureRect();
            checkboxEnt.addComponent<cro::Callback>().active = true;
            checkboxEnt.getComponent<cro::Callback>().function =
                [&, bounds](cro::Entity en, float)
            {
                auto b = bounds;
                if (m_matchMaking.getFriendsOnly())
                {
                    b.bottom -= bounds.height;
                }
                en.getComponent<cro::Sprite>().setTextureRect(b);
            };

            e.getComponent<cro::Transform>().addChild(checkboxEnt.getComponent<cro::Transform>());


            checkboxEnt = m_uiScene.createEntity();
            checkboxEnt.addComponent<cro::Transform>().setPosition({ 188.f, 20.f, 0.1f });
            checkboxEnt.addComponent<cro::AudioEmitter>() = m_menuSounds.getEmitter("switch");
            checkboxEnt.addComponent<cro::Drawable2D>();
            checkboxEnt.addComponent<cro::Sprite>() = m_sprites[SpriteID::LobbyCheckboxHighlight];
            checkboxEnt.getComponent<cro::Sprite>().setColour(cro::Colour::Transparent);
            checkboxEnt.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseSelect;
            bounds = checkboxEnt.getComponent<cro::Sprite>().getTextureBounds();
            checkboxEnt.addComponent<cro::UIInput>().area = bounds;
            checkboxEnt.getComponent<cro::UIInput>().setGroup(MenuID::Lobby);
            checkboxEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Selected] = m_tableSelectCallbacks.selectHighlight;
            checkboxEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::Unselected] = m_tableSelectCallbacks.unselectHighlight;
            checkboxEnt.getComponent<cro::UIInput>().callbacks[cro::UIInput::ButtonUp] = m_tableSelectCallbacks.toggleFriendsOnly;
            e.getComponent<cro::Transform>().addChild(checkboxEnt.getComponent<cro::Transform>());

            auto labelEnt = m_uiScene.createEntity();
            labelEnt.addComponent<cro::Transform>().setPosition({ 200.f, 28.f, 0.1f });
            labelEnt.addComponent<cro::Drawable2D>();
            labelEnt.addComponent<cro::Text>(font).setString("Friends Only");
            labelEnt.getComponent<cro::Text>().setCharacterSize(InfoTextSize);
            labelEnt.getComponent<cro::Text>().setFillColour(TextNormalColour);
            labelEnt.addComponent<cro::CommandTarget>().ID = CommandID::Menu::CourseSelect;
            e.getComponent<cro::Transform>().addChild(labelEnt.getComponent<cro::Transform>());
        };
        m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
    }
}

void ClubhouseState::updateLobbyAvatars()
{
    cro::Command cmd;
    cmd.targetFlags = CommandID::Menu::LobbyList;
    cmd.action = [&](cro::Entity e, float)
    {
        //remove old data
        auto& children = e.getComponent<cro::Callback>().getUserData<std::vector<cro::Entity>>();
        for (auto c : children)
        {
            m_uiScene.destroyEntity(c);
        }
        children.clear();

        auto& font = m_sharedData.sharedResources->fonts.get(FontID::UI);

        auto createName = [&](const cro::String& name, float positionOffset)
        {
            auto entity = m_uiScene.createEntity();
            entity.addComponent<cro::Transform>().setPosition({ positionOffset, -4.f, 0.2f });
            entity.getComponent<cro::Transform>().move(e.getComponent<cro::Transform>().getOrigin());
            entity.addComponent<cro::Drawable2D>();
            entity.addComponent<cro::Text>(font).setCharacterSize(UITextSize);
            entity.getComponent<cro::Text>().setFillColour(TextNormalColour);
            entity.getComponent<cro::Text>().setString(name.substr(0, ConstVal::MaxStringChars));
            auto bounds = cro::Text::getLocalBounds(entity);
            entity.addComponent<NameScroller>().maxDistance = bounds.width - NameWidth;
            bounds.width = NameWidth;
            entity.getComponent<cro::Drawable2D>().setCroppingArea(bounds);
            centreText(entity);
            e.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
            children.push_back(entity);
        };

        static constexpr float PositionOffset = 80.f;
        if (m_sharedData.localConnectionData.playerCount == 2)
        {
            //create both player names
            for (auto i = 0u; i < 2u; ++i)
            {
                createName(m_sharedData.localConnectionData.playerData[i].name, -PositionOffset + (i * (PositionOffset * 2.f)));
            }
        }
        else
        {
            //create separate names and add readiness indicator
            std::int32_t clientCount = 0;
            for (auto i = 0u; i < 2u; ++i)
            {
                if (m_sharedData.connectionData[i].playerCount)
                {
                    float x = -PositionOffset + (i * (PositionOffset * 2.f));
                    createName(m_sharedData.connectionData[i].playerData[0].name, x);

                    static constexpr float IconOffset = 140.f;
                    x = -IconOffset + (i * (IconOffset * 2.f));

                    auto entity = m_uiScene.createEntity();
                    entity.addComponent<cro::Transform>().setPosition({x, -8.f});
                    entity.getComponent<cro::Transform>().move(e.getComponent<cro::Transform>().getOrigin());
                    entity.addComponent<cro::Drawable2D>();
                    entity.addComponent<cro::Sprite>() = m_sprites[SpriteID::ReadyStatus];
                    auto bounds = entity.getComponent<cro::Sprite>().getTextureBounds();
                    entity.getComponent<cro::Transform>().setOrigin({ bounds.width / 2.f, bounds.height / 2.f });
                    entity.addComponent<cro::Callback>().active = true;
                    entity.getComponent<cro::Callback>().function =
                        [&, i](cro::Entity e2, float)
                    {
                        cro::Colour colour = m_readyState[i] ? TextGreenColour : LeaderboardTextDark;
                        e2.getComponent<cro::Sprite>().setColour(colour);
                    };
                    e.getComponent<cro::Transform>().addChild(entity.getComponent<cro::Transform>());
                    children.push_back(entity);

                    clientCount++;
                }
                else
                {
                    createName("Waiting...", -PositionOffset + (i * (PositionOffset * 2.f)));
                }
            }
            auto strClientCount = std::to_string(clientCount);
            Social::setStatus(Social::InfoID::Lobby, { "Billiards", strClientCount.c_str(), "2" });
            Social::setGroup(m_sharedData.lobbyID, clientCount);
        }

    };
    m_uiScene.getSystem<cro::CommandSystem>()->sendCommand(cmd);
}

void ClubhouseState::updateBallTexture()
{
    auto ballTex = cro::TextureID(m_resources.textures.get(m_tableData[m_tableIndex].ballSkins[m_tableData[m_tableIndex].ballSkinIndex]));
    m_previewBalls[0].getComponent<cro::Model>().setMaterialProperty(0, "u_diffuseMap", ballTex);
    m_previewBalls[1].getComponent<cro::Model>().setMaterialProperty(0, "u_diffuseMap", ballTex);
}