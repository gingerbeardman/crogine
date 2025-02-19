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

#include "MyApp.hpp"
#include "MainState.hpp"
#include "MenuState.hpp"
#include "GameState.hpp"
#include "PauseState.hpp"
#include "GameOverState.hpp"
#include "RoundEndState.hpp"
#include "LoadingScreen.hpp"
#include "icon.hpp"
#include "Messages.hpp"

#include <crogine/core/Clock.hpp>
#include <crogine/audio/AudioMixer.hpp>

MyApp::MyApp()
    : m_stateStack({*this, getWindow()})
{
    //register states
#ifdef PLATFORM_MOBILE
    //m_stateStack.registerState<MenuState>(States::ID::MainMenu);
    m_stateStack.registerState<MainState>(States::ID::MainMenu, m_sharedResources);
#else
    m_stateStack.registerState<MainState>(States::ID::MainMenu, m_sharedResources);
#endif //PLATFORM_MOBILE

    m_stateStack.registerState<GameState>(States::ID::GamePlaying);
    m_stateStack.registerState<PauseState>(States::ID::PauseMenu, m_sharedResources);
    m_stateStack.registerState<GameOverState>(States::ID::GameOver, m_sharedResources);
    m_stateStack.registerState<RoundEndState>(States::ID::RoundEnd, m_sharedResources);
}

//public
void MyApp::handleEvent(const cro::Event& evt)
{
    if (evt.type == SDL_KEYUP)
    {
        switch (evt.key.keysym.sym)
        {
        default: break;
#ifdef CRO_DEBUG_
        case SDLK_ESCAPE:
#endif
        case SDLK_AC_BACK:
            App::quit();
            break;
        }
    }
    
    m_stateStack.handleEvent(evt);
}

void MyApp::handleMessage(const cro::Message& msg)
{
    if (msg.id == MessageID::GameMessage)
    {
        const auto& data = msg.getData<GameEvent>();
        switch (data.type)
        {
        default: break;
        case GameEvent::GameStart:
            m_sharedResources->playerName = "";
            m_sharedResources->score = 0;
            break;
        }
    }
    else if (msg.id == MessageID::StatsMessage)
    {
        const auto& data = msg.getData<StatsEvent>();
        if (data.type == StatsEvent::Score)
        {
            m_sharedResources->score = data.value;
        }
    }

    m_stateStack.handleMessage(msg);
}

void MyApp::simulate(float dt)
{
    m_stateStack.simulate(dt);
}

void MyApp::render()
{
    m_stateStack.render();
}

bool MyApp::initialise()
{
    getWindow().setLoadingScreen<LoadingScreen>();
    getWindow().setIcon(icon);
    getWindow().setTitle("Threat Level");

    m_sharedResources = std::make_unique<SharedResources>();
    m_stateStack.pushState(States::MainMenu);
    //m_stateStack.pushState(States::GamePlaying);

    cro::AudioMixer::setLabel("Music", 0);
    cro::AudioMixer::setLabel("FX", 1);

    return true;
}

void MyApp::finalise()
{
    m_stateStack.clearStates();
    m_stateStack.simulate(0.f);

    m_sharedResources.reset();
}