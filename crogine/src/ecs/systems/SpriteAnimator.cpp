/*-----------------------------------------------------------------------

Matt Marchant 2017 - 2020
http://trederia.blogspot.com

crogine - Zlib license.

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

#include <crogine/ecs/systems/SpriteAnimator.hpp>
#include <crogine/ecs/components/Sprite.hpp>
#include <crogine/ecs/components/SpriteAnimation.hpp>

#include <crogine/core/Clock.hpp>
#include <crogine/core/Message.hpp>

using namespace cro;

namespace
{
    constexpr std::size_t MaxEvents = 12;
}

SpriteAnimator::SpriteAnimator(MessageBus& mb)
    : System(mb, typeid(SpriteAnimator))
{
    requireComponent<Sprite>();
    requireComponent<SpriteAnimation>();

    m_animationEvents.reserve(MaxEvents);
}

//public
void SpriteAnimator::process(float dt)
{
    m_animationEvents.clear();

    auto& entities = getEntities();
    for (auto& entity : entities) 
    {
        auto& animation = entity.getComponent<SpriteAnimation>();

        if (animation.playing)
        {
            auto& sprite = entity.getComponent<Sprite>();

            //TODO we need to somehow make sure this never gets
            //set out of range - however the anim component doesn't
            //know how many animations there are - given that they
            //are stored in the sprite and could theoretically change at any time...
            //CRO_ASSERT(animation.id < sprite.m_animations.size(), "");
            if (animation.id >= static_cast<std::int32_t>(sprite.m_animations.size()))
            {
                animation.stop();
                continue;
            }

            //TODO this should be an assertion as we should never have
            //tried playing the animation in the first place...
            if (sprite.m_animations[animation.id].frames.empty())
            {
                animation.stop();
                continue;
            }
            //really these two cases should be fixed by moving the frame
            //data into the animation component, however this will break sprite sheets.
            
            const auto frameTime = (1.f / (sprite.m_animations[animation.id].framerate * animation.playbackRate));
            animation.currentFrameTime = std::min(animation.currentFrameTime - dt, frameTime);
            if (animation.currentFrameTime < 0)
            {
                CRO_ASSERT(sprite.m_animations[animation.id].framerate > 0, "");
                CRO_ASSERT(animation.playbackRate > 0, "");
                animation.currentFrameTime += frameTime;

                auto lastFrame = animation.frameID;
                animation.frameID = (animation.frameID + 1) % sprite.m_animations[animation.id].frames.size();

                if (animation.frameID < lastFrame)
                {
                    if (!sprite.m_animations[animation.id].looped)
                    {
                        animation.stop();
                        continue;
                    }
                    else
                    {
                        animation.frameID = std::max(animation.frameID, sprite.m_animations[animation.id].loopStart);
                    }
                }

                const auto& frame = sprite.m_animations[animation.id].frames[animation.frameID];
                sprite.setTextureRect(frame.frame);

                if (frame.event != -1
                    && m_animationEvents.size() < MaxEvents)
                {
                    m_animationEvents.emplace_back(entity, frame.event);
                }
            }
        }
    }

    for (const auto& [entity, eventID] : m_animationEvents)
    {
        auto* msg = postMessage<cro::Message::SpriteAnimationEvent>(cro::Message::SpriteAnimationMessage);
        msg->entity = entity;
        msg->userType = eventID;
    }
}