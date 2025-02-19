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

#include "../PacketIDs.hpp"
#include "../CommonConsts.hpp"
#include "../SharedStateData.hpp"
#include "../Utility.hpp"
#include "ServerLobbyState.hpp"
#include "ServerPacketData.hpp"
#include "ServerMessages.hpp"

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
    std::fill(m_readyState.begin(), m_readyState.end(), false);
}

void LobbyState::handleMessage(const cro::Message& msg)
{
    if (msg.id == MessageID::ConnectionMessage)
    {
        const auto& data = msg.getData<ConnectionEvent>();
        if (data.type == ConnectionEvent::Disconnected)
        {
            m_readyState[data.clientID] = false;
        }
    }
}

void LobbyState::netEvent(const net::NetEvent& evt)
{
    if (evt.type == net::NetEvent::PacketReceived)
    {
        switch (evt.packet.getID())
        {
        default:break;
        case PacketID::PlayerInfo:
            insertPlayerInfo(evt);
            break;
        case PacketID::NewLobbyReady:
            m_sharedData.host.broadcastPacket(PacketID::NewLobbyReady, evt.packet.as<std::uint64_t>(), net::NetFlag::Reliable);
            break;
        case PacketID::LobbyReady:
        {
            std::uint16_t data = evt.packet.as<std::uint16_t>();
            m_readyState[((data & 0xff00) >> 8)] = (data & 0x00ff) ? true : false;
            m_sharedData.host.broadcastPacket(PacketID::LobbyReady, data, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
        }
            break;
        case PacketID::MapInfo:
        {
            if (evt.peer.getID() == m_sharedData.hostID)
            {
                m_sharedData.mapDir = deserialiseString(evt.packet);
                //forward to all clients
                m_sharedData.host.broadcastPacket(PacketID::MapInfo, evt.packet.getData(), evt.packet.getSize(), net::NetFlag::Reliable, ConstVal::NetChannelStrings);
            }
        }
            break;
        case PacketID::ScoreType:
            if (evt.peer.getID() == m_sharedData.hostID)
            {
                m_sharedData.scoreType = evt.packet.as<std::uint8_t>();
                m_sharedData.host.broadcastPacket(PacketID::ScoreType, m_sharedData.scoreType, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
            }
            break;
        case PacketID::GimmeRadius:
            if (evt.peer.getID() == m_sharedData.hostID)
            {
                m_sharedData.gimmeRadius = evt.packet.as<std::uint8_t>();
                m_sharedData.host.broadcastPacket(PacketID::GimmeRadius, m_sharedData.gimmeRadius, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
            }
            break;
        case PacketID::HoleCount:
            if (evt.peer.getID() == m_sharedData.hostID)
            {
                m_sharedData.holeCount = evt.packet.as<std::uint8_t>();
                m_sharedData.host.broadcastPacket(PacketID::HoleCount, m_sharedData.holeCount, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
            }
            break;
        case PacketID::ReverseCourse:
            if (evt.peer.getID() == m_sharedData.hostID)
            {
                m_sharedData.reverseCourse = evt.packet.as<std::uint8_t>();
                m_sharedData.host.broadcastPacket(PacketID::ReverseCourse, m_sharedData.reverseCourse, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
            }
            break;
        case PacketID::RequestGameStart:
            if (evt.peer.getID() == m_sharedData.hostID)
            {
                auto state = evt.packet.as<std::uint8_t>();
                if (state < StateID::Count)
                {
                    m_returnValue = state;
                }
                //TODO throw a server error if this is a weird value
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
void LobbyState::insertPlayerInfo(const net::NetEvent& evt)
{
    //find the connection index
    std::uint8_t connectionID = 4;
    for(auto i = 0u; i < ConstVal::MaxClients; ++i)
    {
        if (m_sharedData.clients[i].peer == evt.peer)
        {
            connectionID = i;
            break;
        }
    }

    if (connectionID < ConstVal::MaxClients)
    {
        if (evt.packet.getSize() > 0)
        {
            ConnectionData cd;
            if (cd.deserialise(evt.packet))
            {
                m_sharedData.clients[connectionID].peerID = cd.peerID;
                m_sharedData.clients[connectionID].playerCount = cd.playerCount;
                for (auto i = 0u; i < cd.playerCount; ++i)
                {
                    m_sharedData.clients[connectionID].playerData[i].name = cd.playerData[i].name;
                    m_sharedData.clients[connectionID].playerData[i].avatarFlags = cd.playerData[i].avatarFlags;
                    m_sharedData.clients[connectionID].playerData[i].ballID = cd.playerData[i].ballID;
                    m_sharedData.clients[connectionID].playerData[i].hairID = cd.playerData[i].hairID;
                    m_sharedData.clients[connectionID].playerData[i].skinID = cd.playerData[i].skinID;
                    m_sharedData.clients[connectionID].playerData[i].flipped = cd.playerData[i].flipped;
                    m_sharedData.clients[connectionID].playerData[i].isCPU = cd.playerData[i].isCPU;
                }
            }
            else
            {
                //reject the client
                m_sharedData.host.sendPacket(evt.peer, PacketID::ConnectionRefused, std::uint8_t(MessageType::BadData), net::NetFlag::Reliable, ConstVal::NetChannelReliable);
                
                auto peer = evt.peer;
                m_sharedData.host.disconnectLater(peer);

                LogE << "Server - rejected client, unable to read player info packet" << std::endl;
            }
        }
    }

    //broadcast all player info (we need to send all clients to the new one, so might as well update everyone)
    for (auto i = 0u; i < ConstVal::MaxClients; ++i)
    {
        const auto& c = m_sharedData.clients[i];
        if (c.connected)
        {
            ConnectionData cd;
            cd.peerID = c.peerID;
            cd.connectionID = static_cast<std::uint8_t>(i);
            cd.playerCount = static_cast<std::uint8_t>(c.playerCount);
            for (auto j = 0u; j < c.playerCount; ++j)
            {
                cd.playerData[j].name = c.playerData[j].name;
                cd.playerData[j].avatarFlags = c.playerData[j].avatarFlags;
                cd.playerData[j].ballID = c.playerData[j].ballID;
                cd.playerData[j].hairID = c.playerData[j].hairID;
                cd.playerData[j].skinID = c.playerData[j].skinID;
                cd.playerData[j].flipped = c.playerData[j].flipped;
                cd.playerData[j].isCPU = c.playerData[j].isCPU;
            }
            auto buffer = cd.serialise();

            m_sharedData.host.broadcastPacket(PacketID::LobbyUpdate, buffer.data(), buffer.size(), net::NetFlag::Reliable, ConstVal::NetChannelStrings);
        }

        std::uint8_t ready = m_readyState[i] ? 1 : 0;
        m_sharedData.host.broadcastPacket(PacketID::LobbyReady, std::uint16_t(std::uint8_t(i) << 8 | ready), net::NetFlag::Reliable, ConstVal::NetChannelReliable);

        auto mapDir = serialiseString(m_sharedData.mapDir);
        m_sharedData.host.broadcastPacket(PacketID::MapInfo, mapDir.data(), mapDir.size(), net::NetFlag::Reliable, ConstVal::NetChannelStrings);

        m_sharedData.host.broadcastPacket(PacketID::ScoreType, m_sharedData.scoreType, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
        m_sharedData.host.broadcastPacket(PacketID::HoleCount, m_sharedData.holeCount, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
        m_sharedData.host.broadcastPacket(PacketID::GimmeRadius, m_sharedData.gimmeRadius, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
        m_sharedData.host.broadcastPacket(PacketID::ReverseCourse, m_sharedData.reverseCourse, net::NetFlag::Reliable, ConstVal::NetChannelReliable);
    }
}