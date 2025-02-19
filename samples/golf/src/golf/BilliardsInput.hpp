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

#pragma once

#include <crogine/core/Window.hpp>
#include <crogine/ecs/Entity.hpp>
#include <crogine/gui/GuiClient.hpp>
#include <crogine/detail/glm/gtc/quaternion.hpp>

struct SharedStateData;

struct ControllerRotation final
{
    float rotation = 0.f; //rads
    bool* activeCamera = nullptr;
};

struct ControlEntities final
{
    cro::Entity camera;
    cro::Entity cameraTilt;
    cro::Entity cue;
    cro::Entity previewBall;
    cro::Entity indicator;
    cro::Entity spectator;
};

class BilliardsInput final : public cro::GuiClient
{
public:
    BilliardsInput(const SharedStateData&, cro::MessageBus&);

    void handleEvent(const cro::Event&);
    void update(float);

    void setActive(bool active, bool placeCueball);
    bool getActive() const { return m_active; }
    void setControlEntities(ControlEntities);
    void setSpawnArea(cro::FloatRect area) { m_spawnArea = area; }

    std::pair<glm::vec3, glm::vec3> getImpulse() const;

    static constexpr float MaxPower = 1.f;
    float getPower() const { return m_power; }

    const glm::quat& getSpinOffset() const { return m_spinOffset; }

    bool canRotate() const { return m_active && m_state == 0; }

    bool hasInput() const { return m_inputFlags != 0 || hasMouseMotion(); }

private:
    const SharedStateData& m_sharedData;
    cro::MessageBus& m_messageBus;

    enum
    {
        Play, PlaceBall
    }m_state = PlaceBall;

    std::uint16_t m_inputFlags;
    std::uint16_t m_prevFlags;
    std::uint16_t m_prevStick;

    glm::vec2 m_mouseMove;
    glm::vec2 m_prevMouseMove;
    float m_analogueAmountLeft;
    float m_analogueAmountRight;

    bool m_active;
    bool m_clampRotation;
    float m_maxRotation;

    float m_power;
    float m_topSpin;
    float m_sideSpin;
    glm::quat m_spinOffset;

    ControlEntities m_controlEntities;
    cro::FloatRect m_spawnArea;

    bool hasMouseMotion() const;

    void checkController(float);
    void updatePlay(float);
    void updatePlaceBall(float);
};
