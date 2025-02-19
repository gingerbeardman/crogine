/*-----------------------------------------------------------------------

Matt Marchant 2020 - 2021
http://trederia.blogspot.com

crogine editor - Zlib license.

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

#pragma once

#include "StateIDs.hpp"

#include <crogine/core/State.hpp>
#include <crogine/gui/GuiClient.hpp>
#include <crogine/gui/Gui.hpp>
#include <crogine/ecs/Scene.hpp>
#include <crogine/graphics/EnvironmentMap.hpp>
#include <crogine/graphics/ModelDefinition.hpp>

#include <memory>

struct SharedStateData;
class WorldState final : public cro::State, public cro::GuiClient
{
public:
    WorldState(cro::StateStack&, cro::State::Context, SharedStateData&);
    ~WorldState();

    cro::StateID getStateID() const override { return States::WorldEditor; }

    bool handleEvent(const cro::Event&) override;
    void handleMessage(const cro::Message&) override;
    bool simulate(float) override;
    void render() override;

private:
    SharedStateData& m_sharedData;
    cro::Scene m_scene;
    cro::Scene m_previewScene; //used to draw model thumbnails
    cro::EnvironmentMap m_environmentMap;
    cro::ResourceCollection m_resources;

    float m_viewportRatio;
    float m_fov;
    ImVec4 m_messageColour;

    void loadAssets();
    void addSystems();
    void setupScene();

    void loadPrefs();
    void savePrefs();

    struct EntityID final
    {
        enum
        {
            ArcBall,

            Count
        };
    };

    std::array<cro::Entity, EntityID::Count> m_entities = {};
    cro::Entity m_selectedEntity;
    std::int32_t m_gizmoMode;

    bool m_showPreferences;

    void initUI();
    void drawMenuBar();
    void drawInspector();
    void drawBrowser();
    void drawInfo();
    void drawGizmo();
    void drawOptions();
    void updateLayout(std::int32_t, std::int32_t);
    void updateMouseInput(const cro::Event&);



    struct ReferenceModel final
    {
        std::size_t modelID = 0;
        std::unique_ptr<cro::ModelDefinition> modelDef;
        cro::RenderTexture thumbnail;
    };
    std::vector<ReferenceModel> m_models;
    std::size_t m_selectedModel;
    cro::Entity m_previewEntity;

    void openModel(const std::string&);
};