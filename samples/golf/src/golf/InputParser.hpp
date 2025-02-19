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

#pragma once

#include "InputBinding.hpp"
#include "Swingput.hpp"

#include <crogine/core/App.hpp>
#include <crogine/core/Clock.hpp>
#include <crogine/detail/glm/vec3.hpp>

namespace cro
{
    class MessageBus;
}

struct SharedStateData;
class InputParser final
{
public:
       
    InputParser(const SharedStateData&, cro::MessageBus&);

    void handleEvent(const cro::Event&);
    void setHoleDirection(glm::vec3);
    void setClub(float); //picks closest club to given distance
    float getYaw() const;

    float getPower() const; //0-1 multiplied by selected club
    float getHook() const; //-1 to -1 * some angle, club defined

    std::int32_t getClub() const;

    void setActive(bool active, bool isCPU = false);
    void setSuspended(bool);
    void setEnableFlags(std::uint16_t); //bits which are set are *enabled*
    void setMaxClub(float); //based on overall distance of hole
    void setMaxClub(std::int32_t); //force set when only wanting wedges for example
    void resetPower();
    void update(float, std::int32_t terrain);

    bool inProgress() const;
    bool getActive() const;

    bool isSwingputActive() const { return m_swingput.isActive(); }
    float getSwingputPosition() const { return m_swingput.getActivePoint().y; }
    void setMouseScale(float scale) { CRO_ASSERT(scale > 0, ""); m_swingput.setMouseScale(scale); }

    void setMaxRotation(float);
    float getMaxRotation() const { return m_maxRotation; }

    const InputBinding getInputBinding() const { return m_inputBinding; }

    static constexpr std::uint32_t CPU_ID = 1337u;

private:
    const SharedStateData& m_sharedData;
    const InputBinding& m_inputBinding;
    cro::MessageBus& m_messageBus;

    Swingput m_swingput;

    std::uint16_t m_inputFlags;
    std::uint16_t m_prevFlags;
    std::uint16_t m_enableFlags;
    std::uint16_t m_prevDisabledFlags; //< for raising events on disabled inputs
    std::uint16_t m_prevStick;
    float m_analogueAmount;
    float m_inputAcceleration;

    std::int32_t m_mouseWheel;
    std::int32_t m_prevMouseWheel;
    std::int32_t m_mouseMove;
    std::int32_t m_prevMouseMove;

    bool m_isCPU;
    cro::Clock m_doubleTapClock; //prevent accidentally double tapping action

    float m_holeDirection; //radians
    float m_rotation; //+- max rads
    float m_maxRotation;

    float m_power;
    float m_hook;
    float m_powerbarDirection;

    bool m_active;
    bool m_suspended;

    enum class State
    {
        Aim, Power, Stroke,
        Flight
    }m_state;

    std::int32_t m_currentClub;
    std::int32_t m_firstClub;
    std::int32_t m_clubOffset; //offset ID from first club

    void rotate(float);
    void checkControllerInput();
    void checkMouseInput();
};