/*-----------------------------------------------------------------------

Matt Marchant 2017
http://trederia.blogspot.com

crogine test application - Zlib license.

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

#include "PlayerSystem.hpp"
#include "VelocitySystem.hpp"
#include "ResourceIDs.hpp"
#include "Messages.hpp"
#include "ItemSystem.hpp"
#include "NpcWeaponSystem.hpp"
#include "PhysicsObject.hpp"

#include <crogine/core/Clock.hpp>
#include <crogine/ecs/Scene.hpp>
#include <crogine/ecs/components/Transform.hpp>
#include <crogine/ecs/components/Sprite.hpp>
#include <crogine/ecs/components/ParticleEmitter.hpp>
#include <crogine/ecs/components/Callback.hpp>
#include <crogine/ecs/systems/CommandSystem.hpp>

#include <crogine/detail/glm/gtx/norm.hpp>

namespace
{
    const glm::vec3 spawnTarget(-3.f, 0.f, -9.3f);
    const glm::vec3 initialPosition(-15.4f, 0.f, -9.3f);
    const glm::vec3 gravity(0.f, -0.4f, 0.f);

    const float shieldTime = 3.f;

    const std::int32_t maxLives = 5;

    const std::int32_t bonusScore = 250000;
}

PlayerSystem::PlayerSystem(cro::MessageBus& mb)
    : cro::System   (mb, typeid(PlayerSystem)),
    m_respawnTime   (0.f),
    m_shieldTime    (shieldTime),
    m_score         (0)
{
    requireComponent<PlayerInfo>();
    requireComponent<cro::Transform>();
    requireComponent<cro::PhysicsObject>();
}

//public
void PlayerSystem::handleMessage(const cro::Message& msg)
{
    if (msg.id == MessageID::NpcMessage)
    {
        const auto& data = msg.getData<NpcEvent>();
        if (data.type == NpcEvent::Died)
        {
            auto oldScore = m_score / bonusScore;
            
            m_score += static_cast<std::int32_t>(data.value);

            //check if we got a bonus extra life
            auto newScore = m_score / bonusScore;
            if (newScore > oldScore)
            {
                //raise extra life message
                cro::Command cmd;
                cmd.targetFlags = CommandID::Player;
                cmd.action = [&](cro::Entity entity, float)
                {
                    auto& playerInfo = entity.getComponent<PlayerInfo>();
                    
                    if (playerInfo.lives < maxLives)
                    {
                        playerInfo.lives++; 

                        auto* msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
                        msg->entityID = entity.getIndex();
                        msg->type = PlayerEvent::GotLife;
                        msg->value = static_cast<float>(playerInfo.lives);
                    }
                };
                getScene()->getSystem<cro::CommandSystem>()->sendCommand(cmd);
            }

            auto* scoreMsg = postMessage<StatsEvent>(MessageID::StatsMessage);
            scoreMsg->type = StatsEvent::Score;
            scoreMsg->value = m_score;
        }
    }
    else if (msg.id == cro::Message::StateMessage)
    {
        auto* scoreMsg = postMessage<StatsEvent>(MessageID::StatsMessage);
        scoreMsg->type = StatsEvent::Score;
        scoreMsg->value = m_score;
    }
}

void PlayerSystem::process(float dt)
{
    auto& entities = getEntities();

    for (auto& entity : entities)
    {
        float scale = m_shieldTime / shieldTime;

        auto shieldEnt = getScene()->getEntity(entity.getComponent<PlayerInfo>().shieldEntity);
        shieldEnt.getComponent<cro::Transform>().setScale(glm::vec3(scale * 2.4f)); //kludge because default sphere models is R0.5

        auto& playerInfo = entity.getComponent<PlayerInfo>();
        //DPRINT("Health", std::to_string(playerInfo.health));
        switch (playerInfo.state)
        {
        case PlayerInfo::State::Spawning:
            updateSpawning(entity, dt);
            break;
        case PlayerInfo::State::Alive:
            updateAlive(entity, dt);
            break;
        case PlayerInfo::State::Dying:
            updateDying(entity, dt);
            break;
        case PlayerInfo::State::Dead:
            updateDead(entity, dt);
            break;
        case PlayerInfo::State::EndingRound:
            updateRoundEnd(entity, dt);
            break;
        }
    }
}

//private
void PlayerSystem::updateSpawning(cro::Entity entity, float dt)
{
    auto& tx = entity.getComponent<cro::Transform>();
    auto dist = spawnTarget - tx.getWorldPosition();
    tx.move(dist * dt * 2.f);

    if (glm::length2(dist) < 0.5f)
    {
        entity.getComponent<PlayerInfo>().state = PlayerInfo::State::Alive;
        entity.getComponent<PlayerInfo>().health = 100.f;
        entity.getComponent<Velocity>().velocity = dist * 2.f;
        
        auto* msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
        msg->entityID = entity.getIndex();
        msg->type = PlayerEvent::Spawned;

        msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
        msg->entityID = entity.getIndex();
        msg->type = PlayerEvent::HealthChanged;
        msg->value = 100.f;
    }
}

void PlayerSystem::updateAlive(cro::Entity entity, float dt)
{
    m_shieldTime = std::max(0.f, m_shieldTime - dt);
    auto& playerInfo = entity.getComponent<PlayerInfo>();

    if (playerInfo.pendingRoundEnd)
    {
        playerInfo.state = PlayerInfo::State::EndingRound;
        playerInfo.pendingRoundEnd = false;
        return;
    }
    
    //Do collision stuff
    const auto& po = entity.getComponent<cro::PhysicsObject>();
    const auto& colliders = po.getCollisionIDs();
    for (auto i = 0u; i < po.getCollisionCount(); ++i)
    {
        auto otherEnt = getScene()->getEntity(colliders[i]);
        const auto& otherPo = otherEnt.getComponent<cro::PhysicsObject>();

        if (((otherPo.getCollisionGroups() & (CollisionID::NPC | CollisionID::Environment)) != 0)
            && m_shieldTime == 0)
        {          
            playerInfo.health -= 3.5f; //TODO make this a convar for difficulty levels

            {
                auto* msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
                msg->entityID = entity.getIndex();
                msg->type = PlayerEvent::HealthChanged;
                msg->value = playerInfo.health;
            }
        }
        else if ((otherPo.getCollisionGroups() & (CollisionID::NpcLaser)) != 0)
        {
            //subtract health based on weapon type.
            if (otherEnt.hasComponent<NpcWeapon>()) //lasers are parented, and weapon is on that entity
            {
                playerInfo.health -= otherEnt.getComponent<NpcWeapon>().damage;
            }
            else
            {
                playerInfo.health = 0.f; //lasers are one hit kill
            }
            auto* msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
            msg->entityID = entity.getIndex();
            msg->type = PlayerEvent::HealthChanged;
            msg->value = playerInfo.health;
        }
        else if ((otherPo.getCollisionGroups() & (CollisionID::Collectable)) != 0)
        {
            //raise message
            const auto& item = otherEnt.getComponent<CollectableItem>();
            auto* msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
            msg->entityID = entity.getIndex();
            msg->type = PlayerEvent::CollectedItem;
            msg->itemID = item.type;

            m_score += item.scoreValue;
            auto* scoreMsg = postMessage<StatsEvent>(MessageID::StatsMessage);
            scoreMsg->type = StatsEvent::Score;
            scoreMsg->value = m_score;

            //update player inventory
            switch (item.type)
            {
            default: break;
            case CollectableItem::Shield:
            {
                playerInfo.health = 100.f;

                auto* msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
                msg->entityID = entity.getIndex();
                msg->type = PlayerEvent::HealthChanged;
                msg->value = playerInfo.health;
            }
                break;
            case CollectableItem::Bomb:
                playerInfo.hasBombs = true;
                break;
            case CollectableItem::EMP:
                playerInfo.hasEmp = true;
                break;
            case CollectableItem::Life:
                if (playerInfo.lives < maxLives)
                {
                    playerInfo.lives++;
                    auto* msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
                    msg->entityID = entity.getIndex();
                    msg->type = PlayerEvent::GotLife;
                    msg->value = static_cast<float>(playerInfo.lives);
                }
                break;
            }
        }
        else if ((otherPo.getCollisionGroups() & (CollisionID::Bounds)) != 0)
        {
            //keep in area
            auto& tx = entity.getComponent<cro::Transform>();
            glm::vec3 normal = glm::vec3(0.f);
            normal.x = (tx.getWorldPosition().x < 0)? 1.f : -1.f;

            //discover penetration and reverse it - TODO is it safe to assume we always have at least one point?
            tx.move({ -normal.x * po.getManifolds()[i].points[0].distance, 0.f, 0.f });

            //reflect vel
            auto& vel = entity.getComponent<Velocity>();
            vel.velocity = glm::reflect(vel.velocity, normal) * 0.3f;
        }
    }

    if (playerInfo.health <= 0)
    {
        playerInfo.state = PlayerInfo::State::Dying;
        playerInfo.lives--;
        playerInfo.hasBombs = false;
        playerInfo.hasEmp = false;
        entity.getComponent<Velocity>().velocity.x = -13.f;

        auto* msg = postMessage<PlayerEvent>(MessageID::PlayerMessage);
        msg->entityID = entity.getIndex();
        msg->type = PlayerEvent::Died;
        msg->value = static_cast<float>(playerInfo.lives);
    }
}

void PlayerSystem::updateDying(cro::Entity entity, float dt)
{
    //drop out of map
    entity.getComponent<Velocity>().velocity += gravity;
    
    auto& tx = entity.getComponent<cro::Transform>();
    tx.rotate({ 1.f, 0.f, 0.f }, 10.f * dt);
    if (tx.getWorldPosition().y < -3.f)
    {
        tx.setPosition(initialPosition);
        tx.setRotation(cro::Transform::QUAT_IDENTY);
        entity.getComponent<Velocity>().velocity = glm::vec3(0.f);
        entity.getComponent<PlayerInfo>().state = PlayerInfo::State::Dead;
        m_respawnTime = 1.f;

        entity.getComponent<cro::ParticleEmitter>().stop();
    }
}

void PlayerSystem::updateDead(cro::Entity entity, float dt)
{
    //count down to respawn
    m_respawnTime -= dt;
    if (m_respawnTime < 0)
    {
        //only respawn if lives remain
        if (entity.getComponent<PlayerInfo>().lives > 0)
        {
            entity.getComponent<PlayerInfo>().state = PlayerInfo::State::Spawning;
            m_shieldTime = shieldTime;
        }
        else
        {
            auto* msg = postMessage<GameEvent>(MessageID::GameMessage);
            msg->type = GameEvent::GameOver;
            //msg->score = m_score;
        }
    }
}

void PlayerSystem::updateRoundEnd(cro::Entity entity, float dt)
{
    auto& tx = entity.getComponent<cro::Transform>();
    if (tx.getWorldPosition().x < 20.f)
    {
        tx.move({ 8.f * dt, 0.f, 0.f });
    }
}

void PlayerSystem::onEntityAdded(cro::Entity entity)
{
    entity.getComponent<PlayerInfo>().maxParticleRate = entity.getComponent<cro::ParticleEmitter>().settings.emitRate;
}