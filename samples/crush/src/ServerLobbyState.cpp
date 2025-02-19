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

#include "ServerLobbyState.hpp"
#include "PacketIDs.hpp"
#include "CommonConsts.hpp"
#include "ServerPacketData.hpp"

#include <crogine/core/Log.hpp>
#include <crogine/network/NetData.hpp>

#include <cstring>

using namespace sv;

LobbyState::LobbyState(SharedData& sd)
    : m_returnValue (StateID::Lobby),
    m_sharedData    (sd)
{
    LOG("Entered Server Lobby State", cro::Logger::Type::Info);

    //this is client readyness to receive map data
    for (auto& c : sd.clients)
    {
        c.ready = false;
    }

    //this is lobby readiness
    for (auto& b : m_readyState)
    {
        b = false;
    }
}

void LobbyState::handleMessage(const cro::Message&)
{

}

void LobbyState::netEvent(const cro::NetEvent& evt)
{
    if (evt.type == cro::NetEvent::PacketReceived)
    {
        switch (evt.packet.getID())
        {
        default:break;
        case PacketID::PlayerInfo:
            insertPlayerInfo(evt);
            break;
        case PacketID::LobbyReady:
        {
            std::uint16_t data = evt.packet.as<std::uint16_t>();
            m_readyState[((data & 0xff00) >> 8)] = (data & 0x00ff) ? true : false;
            m_sharedData.host.broadcastPacket(PacketID::LobbyReady, data, cro::NetFlag::Reliable, ConstVal::NetChannelReliable);
        }
            break;
        case PacketID::PlayerCount:
        {
            auto data = evt.packet.as<std::uint16_t>();
            std::uint8_t connection = (data & 0xff00) >> 8;
            std::uint8_t count = (data & 0x00ff);
            //TODO make sure the sender is local
            m_sharedData.clients.at(connection).playerCount = count;
        }
            break;
        case PacketID::MapName:
            updateMapName(evt);
            break;
        case PacketID::RequestGame:
            if (evt.packet.as<std::uint8_t>() == 1)
            {
                //assert sender is host - this assumes the host is always first to connect
                if (evt.peer == m_sharedData.clients[0].peer)
                {
                    m_returnValue = sv::StateID::Game;
                    m_sharedData.host.broadcastPacket(PacketID::StateChange, std::uint8_t(sv::StateID::Game), cro::NetFlag::Reliable, ConstVal::NetChannelReliable);
                }
            }
            break;
        }
    }
}

std::int32_t LobbyState::process(float dt)
{
    return m_returnValue;
}

//private
void LobbyState::insertPlayerInfo(const cro::NetEvent& evt)
{
    std::uint8_t playerID = 4;
    for(auto i = 0u; i < ConstVal::MaxClients; ++i)
    {
        if (m_sharedData.clients[i].peer == evt.peer)
        {
            playerID = i;
            break;
        }
    }

    if (playerID < ConstVal::MaxClients)
    {
        m_sharedData.clients[playerID].name = Util::readStringPacket(evt.packet);
    }

    //broadcast all player info (we need to send all clients to the new one, so might as well update everyone)
    for (auto i = 0u; i < ConstVal::MaxClients; ++i)
    {
        const auto& c = m_sharedData.clients[i];
        if (c.connected)
        {
            LobbyData data;
            data.playerID = static_cast<std::uint8_t>(i);
            data.skinFlags = 0;
            data.stringSize = static_cast<std::uint8_t>(c.name.size() * sizeof(std::uint32_t)); //we're not checking valid size here because we assume it was validated on arrival

            std::vector<std::uint8_t> buffer(data.stringSize + sizeof(LobbyData));
            std::memcpy(buffer.data(), &data, sizeof(data));
            std::memcpy(buffer.data() + sizeof(data), c.name.data(), data.stringSize);

            m_sharedData.host.broadcastPacket(PacketID::LobbyUpdate, buffer.data(), buffer.size(), cro::NetFlag::Reliable, ConstVal::NetChannelStrings);

            //broadcast current map name
            auto mapBuffer = Util::createStringPacket(m_sharedData.mapName);
            m_sharedData.host.broadcastPacket(PacketID::MapName, mapBuffer.data(), mapBuffer.size(), cro::NetFlag::Reliable, ConstVal::NetChannelStrings);
        }

        std::uint8_t ready = m_readyState[i] ? 1 : 0;
        m_sharedData.host.broadcastPacket(PacketID::LobbyReady, std::uint16_t(std::uint8_t(i) << 8 | ready), cro::NetFlag::Reliable, ConstVal::NetChannelReliable);
    }
}

void LobbyState::updateMapName(const cro::NetEvent& evt)
{
    m_sharedData.mapName = Util::readStringPacket(evt.packet);

    m_sharedData.host.broadcastPacket(PacketID::MapName, evt.packet.getData(), evt.packet.getSize(), cro::NetFlag::Reliable, ConstVal::NetChannelStrings);

    LogI << "Server set map to " << m_sharedData.mapName.toAnsiString() << std::endl;
}