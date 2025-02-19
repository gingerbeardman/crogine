/*-----------------------------------------------------------------------

Matt Marchant 2017 - 2022
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

#include <crogine/graphics/DepthTexture.hpp>

#include "../detail/GLCheck.hpp"

using namespace cro;

DepthTexture::DepthTexture()
    : m_fboID   (0),
    m_textureID (0),
    m_size      (0,0),
    m_layerCount(0)
{

}

DepthTexture::~DepthTexture()
{
#ifndef __APPLE__
    if (!m_layerHandles.empty())
    {
        glCheck(glDeleteTextures(static_cast<GLsizei>(m_layerHandles.size()), m_layerHandles.data()));
    }
#endif

    if (m_fboID)
    {
        glCheck(glDeleteFramebuffers(1, &m_fboID));
    }

    if (m_textureID)
    {
        glCheck(glDeleteTextures(1, &m_textureID));
    }
}

DepthTexture::DepthTexture(DepthTexture&& other) noexcept
    : DepthTexture()
{
    m_fboID = other.m_fboID;
    m_textureID = other.m_textureID;
    setViewport(other.getViewport());
    setView(other.getView());
    m_layerCount = other.m_layerCount;

    other.m_fboID = 0;
    other.m_textureID = 0;
    other.setViewport({ 0, 0, 0, 0 });
    other.setView({ 0.f, 0.f });
    other.m_layerCount = 0;

#ifndef __APPLE__
    m_layerHandles.swap(other.m_layerHandles);
#endif
}

DepthTexture& DepthTexture::operator=(DepthTexture&& other) noexcept
{
    if (&other != this)
    {
#ifndef __APPLE__
        if (!m_layerHandles.empty())
        {
            glCheck(glDeleteTextures(static_cast<GLsizei>(m_layerHandles.size()), m_layerHandles.data()));
            m_layerHandles.clear();
        }

        m_layerHandles.swap(other.m_layerHandles);
#endif 

        //tidy up anything we own first!
        if (m_fboID)
        {
            glCheck(glDeleteFramebuffers(1, &m_fboID));
        }

        if (m_textureID)
        {
            glCheck(glDeleteTextures(1, &m_textureID));
        }

        m_fboID = other.m_fboID;
        m_textureID = other.m_textureID;
        setViewport(other.getViewport());
        setView(other.getView());
        m_layerCount = other.m_layerCount;

        other.m_fboID = 0;
        other.m_textureID = 0;
        other.setViewport({ 0, 0, 0, 0 });
        other.setView({ 0.f, 0.f });
        other.m_layerCount = 0;
    }
    return *this;
}

//public
bool DepthTexture::create(std::uint32_t width, std::uint32_t height, std::uint32_t layers)
{
#ifdef PLATFORM_MOBILE
    LogE << "Depth Textures are not available on mobile platforms" << std::endl;
    return false;
#else
    CRO_ASSERT(layers > 0, "");

    if (width == m_size.x && height == m_size.y && layers == m_layerCount)
    {
        //don't do anything
        return true;
    }

    if (m_textureID)
    {
#ifdef GL41
        //resize the buffer
        glCheck(glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureID));
        glCheck(glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT, width, height, layers, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL));

        setViewport({ 0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height) });
        setView(FloatRect(getViewport()));
        m_size = { width, height };
        m_layerCount = layers;

        return true;
#else
        //else we have to regenerate it as it's immutable
        glCheck(glDeleteTextures(1, &m_textureID));
#endif
    }

    //else create it
    m_size = { 0, 0 };
    setViewport({ 0, 0, 0, 0 });

    //create the texture
    glCheck(glGenTextures(1, &m_textureID));
    glCheck(glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureID));
#ifdef GL41
    //apple drivers don't support immutable textures.
    glCheck(glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT, width, height, layers, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL));
#else
    glCheck(glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT24, width, height, layers));
#endif
    glCheck(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glCheck(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glCheck(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
    glCheck(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
    const float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glCheck(glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor));
    

    //create the frame buffer
    if (m_fboID == 0)
    {
        glCheck(glGenFramebuffers(1, &m_fboID));
    }
    glCheck(glBindFramebuffer(GL_FRAMEBUFFER, m_fboID));
    glCheck(glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_textureID, 0, 0));
    glCheck(glDrawBuffer(GL_NONE));
    glCheck(glReadBuffer(GL_NONE));

    auto result = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    if (result)
    {
        setViewport({ 0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height) });
        setView(FloatRect(getViewport()));
        m_size = { width, height };
        m_layerCount = layers;
        updateHandles();
    }

    return result;
#endif
}

glm::uvec2 DepthTexture::getSize() const
{
    return m_size;
}

void DepthTexture::clear(std::uint32_t layer)
{
#ifdef PLATFORM_DESKTOP
    CRO_ASSERT(m_fboID, "No FBO created!");
    CRO_ASSERT(m_layerCount > layer, "");

    //store active buffer and bind this one
    setActive(true);

    //TODO does checking to see we're not already on the
    //active layer take less time than just setting it every time?
    glCheck(glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_textureID, 0, layer));

    glCheck(glColorMask(false, false, false, false));

    //clear buffer - UH OH this will clear the main buffer if FBO is null
    glCheck(glClear(GL_DEPTH_BUFFER_BIT));
#endif
}

void DepthTexture::display()
{
#ifdef PLATFORM_DESKTOP
    glCheck(glColorMask(true, true, true, true));

    //unbind buffer
    setActive(false);
#endif
}

TextureID DepthTexture::getTexture() const
{
    return TextureID(m_textureID, true);
}

TextureID DepthTexture::getTexture(std::uint32_t index) const
{
#ifdef GL41
    return TextureID(0);
#else
    CRO_ASSERT(index < m_layerHandles.size(), "Layer doesn't exist");
    return TextureID(m_layerHandles[index]);
#endif
}

//private
void DepthTexture::updateHandles()
{
#ifndef GL41
    if (!m_layerHandles.empty())
    {
        //this assumes we've recreated a depth texture (we have to, it's immutable)
        //so we have to delete all the viewtextures and create new ones
        glCheck(glDeleteTextures(static_cast<std::uint32_t>(m_layerHandles.size()), m_layerHandles.data()));
    }

    m_layerHandles.resize(m_layerCount);
    glCheck(glGenTextures(m_layerCount, m_layerHandles.data()));

    for (auto i = 0u; i < m_layerHandles.size(); ++i)
    {
        glCheck(glTextureView(m_layerHandles[i], GL_TEXTURE_2D, m_textureID, GL_DEPTH_COMPONENT24, 0, 1, i, 1));
    }
#endif
}