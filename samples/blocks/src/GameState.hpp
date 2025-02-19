/*-----------------------------------------------------------------------

Matt Marchant 2020
http://trederia.blogspot.com

crogine application - Zlib license.

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
#include "ResourceIDs.hpp"
#include "InputParser.hpp"
#include "ServerPacketData.hpp"
#include "ChunkManager.hpp"
#include "Voxel.hpp"

#include <crogine/core/State.hpp>
#include <crogine/core/ConsoleClient.hpp>
#include <crogine/gui/GuiClient.hpp>
#include <crogine/ecs/Scene.hpp>
#include <crogine/graphics/ModelDefinition.hpp>
#include <crogine/network/NetData.hpp>

struct SharedStateData;

namespace cro
{
    struct Camera;
}

class GameState final : public cro::State, public cro::GuiClient, public cro::ConsoleClient
{
public:
    GameState(cro::StateStack&, cro::State::Context, SharedStateData&);
    ~GameState() = default;

    cro::StateID getStateID() const override { return States::Game; }

    bool handleEvent(const cro::Event&) override;
    void handleMessage(const cro::Message&) override;
    bool simulate(float) override;
    void render() override;

private:
    //hm is this dangerous to put here
    //because multiple class can request concurrent access?
    ChunkManager m_chunkManager;
    vx::DataManager m_voxelData;


    SharedStateData& m_sharedData;
    cro::Scene m_gameScene;
    cro::Scene m_uiScene;
    std::uint32_t m_fontID;

    cro::ResourceCollection m_resources;
    std::array<std::int32_t, MaterialID::Count> m_materialIDs = {};
    std::array<std::size_t, MeshID::Count> m_meshIDs = {};

    InputParser m_inputParser;

    cro::Clock m_bitrateClock; //< updates the bitrate display in the debug window
    cro::Clock m_sceneRequestClock; //< spaces the request for initial scene data

    void addSystems();
    void loadAssets();
    void createScene();
    void createUI();

    void updateView(cro::Camera&);

    void handlePacket(const cro::NetEvent::Packet&);
    void spawnPlayer(PlayerInfo);

    std::size_t m_cameraPosIndex;
    void updateCameraPosition();//< switch 3rd to first person view etc
};