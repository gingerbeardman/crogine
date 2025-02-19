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

#include <crogine/ecs/systems/SpriteSystem2D.hpp>
#include <crogine/ecs/components/Sprite.hpp>
#include <crogine/ecs/components/Drawable2D.hpp>
#include <crogine/graphics/Texture.hpp>

using namespace cro;

SpriteSystem2D::SpriteSystem2D(MessageBus& mb)
    : System(mb, typeid(SpriteSystem2D))
{
    requireComponent<Sprite>();
    requireComponent<Drawable2D>();
}

//public
void SpriteSystem2D::process(float)
{
    auto& entities = getEntities();
    for (auto entity : entities)
    {
        auto& sprite = entity.getComponent<Sprite>();
        if (sprite.m_dirtyFlags)
        {
            auto& drawable = entity.getComponent<Drawable2D>();
            auto& verts = drawable.getVertexData();

            if (sprite.m_dirtyFlags == Sprite::DirtyFlags::Colour)
            {
                //only need to update existing vertices
                for (auto& v : verts)
                {
                    v.colour = sprite.m_colour;
                }
            }
            else
            {
                //rebuild the sprite
                auto subRect = sprite.m_textureRect;
                verts.resize(4);

                verts[0].position = { 0.f, subRect.height };
                verts[1].position = { 0.f, 0.f };
                verts[2].position = { subRect.width, subRect.height };
                verts[3].position = { subRect.width, 0.f };

                //update vert coords
                if (sprite.m_texture)
                {
                    glm::vec2 texSize = sprite.m_texture->getSize();
                    verts[0].UV = { subRect.left / texSize.x, (subRect.bottom + subRect.height) / texSize.y };
                    verts[1].UV = { subRect.left / texSize.x, subRect.bottom / texSize.y };
                    verts[2].UV = { (subRect.left + subRect.width) / texSize.x, (subRect.bottom + subRect.height) / texSize.y };
                    verts[3].UV = { (subRect.left + subRect.width) / texSize.x, subRect.bottom / texSize.y };
                }
                //update colour
                verts[0].colour = sprite.m_colour;
                verts[1].colour = sprite.m_colour;
                verts[2].colour = sprite.m_colour;
                verts[3].colour = sprite.m_colour;

                drawable.setTexture(sprite.m_texture);
                drawable.updateLocalBounds();
            }

            sprite.m_dirtyFlags = 0;
        }
    }
}

//private
void SpriteSystem2D::onEntityAdded(Entity entity)
{
    //this applies a blend mode if it was loaded from a sprite sheet
    if (entity.getComponent<Sprite>().m_overrideBlendMode)
    {
        //set blendmode
        entity.getComponent<Drawable2D>().setBlendMode(entity.getComponent<Sprite>().m_blendMode);
    }
}