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

#include "TutorialDirector.hpp"
#include "MessageIDs.hpp"
#include "SharedStateData.hpp"
#include "InputParser.hpp"
#include "Clubs.hpp"
#include "Terrain.hpp"
#include "../StateIDs.hpp"

#include <crogine/ecs/Scene.hpp>
#include <crogine/ecs/components/Callback.hpp>
#include <crogine/core/GameController.hpp>

/*
Tutorial 1 triggered on scene transition completion
Tutorial 2 waits for the player to select the driver
Tutorial 3 lets the player take shots until they land on the green
*/

TutorialDirector::TutorialDirector(SharedStateData& sd, InputParser& ip)
    : m_sharedData      (sd),
    m_inputParser       (ip)
{
    ip.setEnableFlags(~(InputFlag::Action | InputFlag::Left | InputFlag::Right));
}

//public
void TutorialDirector::handleMessage(const cro::Message& msg)
{
    switch (msg.id)
    {
    default: break;
    case MessageID::SceneMessage:
    {
        const auto& data = msg.getData<SceneEvent>();
        switch (data.type)
        {
        default: break;
        case SceneEvent::TransitionComplete:
        {
            //push first tutorial
            m_sharedData.tutorialIndex = 0;

            auto* msg2 = postMessage<SystemEvent>(MessageID::SystemMessage);
            msg2->data = StateID::Tutorial;
            msg2->type = SystemEvent::StateRequest;
        }
        break;
        }
    }
        break;
    case cro::Message::StateMessage:
    {
        //count the number of popped tutorials
        //and apply any input blocking as necessary
        const auto& data = msg.getData<cro::Message::StateEvent>();
        if (data.action == cro::Message::StateEvent::Popped
            && data.id == StateID::Tutorial)
        {
            m_sharedData.tutorialIndex++;

            switch (m_sharedData.tutorialIndex)
            {
            default: break;
            case 2:
                //allow aiming
                m_inputParser.setEnableFlags(~(InputFlag::Action/* | InputFlag::PrevClub | InputFlag::NextClub*/));
                break;
            case 3:
                //allow all input
                m_inputParser.setEnableFlags(InputFlag::All);
                break;
            }
        }
    }
        break;
    case MessageID::SystemMessage:
    {
        const auto& data = msg.getData<SystemEvent>();
        if (data.type == SystemEvent::InputActivated)
        {
            switch (data.data)
            {
            default: break;
            case InputFlag::Action:
                if (m_sharedData.tutorialIndex == 1
                  || m_sharedData.tutorialIndex == 2)
                {
                    //player pressed action after setting club or setting aim
                    auto* msg2 = postMessage<SystemEvent>(MessageID::SystemMessage);
                    msg2->data = StateID::Tutorial;
                    msg2->type = SystemEvent::StateRequest;
                }
                break;
            }
        }
    }
        break;
    case MessageID::GolfMessage:
    {
        const auto& data = msg.getData<GolfEvent>();
        if (data.type == GolfEvent::SetNewPlayer)
        {
            switch (m_sharedData.tutorialIndex)
            {
            default: break;
            case 3:
                if (cro::GameController::getControllerCount() != 0)
                {
                    //swingput tutorial
                    auto* msg2 = postMessage<SystemEvent>(MessageID::SystemMessage);
                    msg2->data = StateID::Tutorial;
                    msg2->type = SystemEvent::StateRequest;
                }
                else
                {
                    //skip to next
                    m_sharedData.tutorialIndex++;
                }
                break;
            case 4:
                if (data.terrain == TerrainID::Green)
                {
                    //final tutorial about putting
                    auto* msg2 = postMessage<SystemEvent>(MessageID::SystemMessage);
                    msg2->data = StateID::Tutorial;
                    msg2->type = SystemEvent::StateRequest;
                }
                break;
            }
        }
    }
        break;
    }
}