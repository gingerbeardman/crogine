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

#include <crogine/ecs/components/Sprite.hpp>
#include <crogine/graphics/Texture.hpp>

using namespace cro;

Sprite::Sprite()
    : m_texture         (nullptr),
    m_colour            (Colour::White),
    m_dirtyFlags        (DirtyFlags::All),
    m_overrideBlendMode (false),
    m_blendMode         (Material::BlendMode::Alpha)
{

}

Sprite::Sprite(const Texture& texture)
    : Sprite()
{
    setTexture(texture);
}

//public
void Sprite::setTexture(const Texture& texture, bool resize)
{
    if (m_texture != &texture)
    {
        m_texture = &texture;
        m_dirtyFlags |= DirtyFlags::Texture;
    }

    if (resize)
    {
        setTextureRect({ glm::vec2(), texture.getSize() });
    }
}

void Sprite::setTextureRect(FloatRect rect)
{
    m_textureRect = rect;
    m_dirtyFlags |= DirtyFlags::Texture;
}

void Sprite::setColour(Colour colour)
{
    m_colour = colour;
    m_dirtyFlags |= DirtyFlags::Colour;
}

FloatRect Sprite::getTextureRectNormalised() const
{
    FloatRect retVal;

    if (m_texture)
    {
        retVal.left = m_textureRect.left / m_texture->getSize().x;
        retVal.width = m_textureRect.width / m_texture->getSize().x;

        retVal.bottom = m_textureRect.bottom / m_texture->getSize().y;
        retVal.height = m_textureRect.height / m_texture->getSize().y;
    }
    return retVal;
}

Colour Sprite::getColour() const
{
    return  m_colour;
}