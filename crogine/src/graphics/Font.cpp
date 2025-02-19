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

/*
Based on the source of SFML's font class
by Laurent Gomila et al https://github.com/SFML/SFML/blob/master/src/SFML/Graphics/Font.cpp 
*/

#include "../detail/DistanceField.hpp"

#include <crogine/graphics/Font.hpp>
#include <crogine/graphics/Image.hpp>
#include <crogine/graphics/Colour.hpp>
#include <crogine/detail/Types.hpp>
#include <crogine/core/FileSystem.hpp>

#include <array>
#include <cstring>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H
#include FT_OUTLINE_H
#include FT_STROKER_H

using namespace cro;

namespace
{
    constexpr float MagicNumber = static_cast<float>(1 << 6);

    //used to create a unique key for bold/outline/codepoint glyphs
    //see https://github.com/SFML/SFML/blob/master/src/SFML/Graphics/Font.cpp#L66
    template <typename T, typename U>
    inline T reinterpret(const U& input)
    {
        CRO_ASSERT(sizeof(T) == sizeof(U), "");
        T output = 0;
        std::memcpy(&output, &input, sizeof(U));
        return output;
    }
    std::uint64_t combine(float outlineThickness, bool bold, std::uint32_t index)
    {
        return (static_cast<std::uint64_t>(reinterpret<std::uint32_t>(outlineThickness)) << 32) | (static_cast<std::uint64_t>(bold) << 31) | index;
    }
}

Font::Font()
    : m_useSmoothing(false)
{

}

Font::~Font()
{
    cleanup();
}

//public
bool Font::loadFromFile(const std::string& filePath)
{
    //remove existing loaded font
    cleanup();

    auto path = FileSystem::getResourcePath() + filePath;

    //init freetype
    FT_Library library;
    if (FT_Init_FreeType(&library) != 0)
    {
        Logger::log("Failed to load font " + path + ": Failed to init freetype", Logger::Type::Error);
        return false;
    }
    m_library = std::make_any<FT_Library>(library);

    //load the face
    RaiiRWops fontFile;
    fontFile.file = SDL_RWFromFile(path.c_str(), "r");
    if (!fontFile.file)
    {
        Logger::log("Failed opening " + path, Logger::Type::Error);
        return false;
    }
    
    m_buffer.clear();
    m_buffer.resize(fontFile.file->size(fontFile.file));
    if (m_buffer.size() == 0)
    {
        Logger::log("Could not open " + path + ": files size was 0", Logger::Type::Error);
        return false;
    }
    SDL_RWread(fontFile.file, m_buffer.data(), m_buffer.size(), 1);


    FT_Face face = nullptr;
    if (FT_New_Memory_Face(library, m_buffer.data(), static_cast<FT_Long>(m_buffer.size()), 0, &face) != 0)
    {
        Logger::log("Failed to load font " + path + ": Failed creating font face", Logger::Type::Error);
        return false;
    }

    //stroker used for rendering outlines
    FT_Stroker stroker = nullptr;
    if (FT_Stroker_New(library, &stroker) != 0)
    {
        LogE << "Failed to load font " << path << ": Failed to create stroker" << std::endl;
        return false;
    }


    //using unicode
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0)
    {
        Logger::log("Failed to load font " + path + ": failed to select unicode charset", Logger::Type::Error);
        FT_Done_Face(face);
        return false;
    }

    m_face = std::make_any<FT_Face>(face);
    m_stroker = std::make_any<FT_Stroker>(stroker);

    return true;
}

Glyph Font::getGlyph(std::uint32_t codepoint, std::uint32_t charSize, bool bold, float outlineThickness) const
{
    auto& currentGlyphs = m_pages[charSize].glyphs;

    auto key = combine(outlineThickness, bold, FT_Get_Char_Index(std::any_cast<FT_Face>(m_face), codepoint));

    auto result = currentGlyphs.find(key);
    if (result != currentGlyphs.end())
    {
        return result->second;
    }
    else
    {
        //add the glyph to the page
        auto glyph = loadGlyph(codepoint, charSize, bold, outlineThickness);
        return currentGlyphs.insert(std::make_pair(key, glyph)).first->second;
    }

    return {};
}

const Texture& Font::getTexture(std::uint32_t charSize) const
{
    //TODO this may return an invalid texture if the
    //current charSize is not inserted in the page map
    //and is automatically created
    return m_pages[charSize].texture;
}

float Font::getLineHeight(std::uint32_t charSize) const
{
    if (m_face.has_value())
    {
        auto face = std::any_cast<FT_Face>(m_face);
        if (face && setCurrentCharacterSize(charSize))
        {
            //there's some magic going on here...
            return static_cast<float>(face->size->metrics.height) / MagicNumber;
        }
    }
    return 0.f;
}

float Font::getKerning(std::uint32_t cpA, std::uint32_t cpB, std::uint32_t charSize) const
{
    if (cpA == 0 || cpB == 0 || !m_face.has_value())
    {
        return 0.f;
    }

    FT_Face face = std::any_cast<FT_Face>(m_face);

    if (face && FT_HAS_KERNING(face) && setCurrentCharacterSize(charSize))
    {
        //convert the characters to indices
        FT_UInt index1 = FT_Get_Char_Index(face, cpA);
        FT_UInt index2 = FT_Get_Char_Index(face, cpB);

        //get the kerning vector
        FT_Vector kerning;
        FT_Get_Kerning(face, index1, index2, FT_KERNING_DEFAULT, &kerning);

        //x advance is already in pixels for bitmap fonts
        if (!FT_IS_SCALABLE(face))
        {
            return static_cast<float>(kerning.x);
        }

        //return the x advance
        return static_cast<float>(kerning.x) / MagicNumber;
    }
    else
    {
        //invalid font, or no kerning
        return 0.f;
    }
}

void Font::setSmooth(bool smooth)
{
    if (smooth != m_useSmoothing)
    {
        m_useSmoothing = smooth;

        for (auto& page : m_pages)
        {
            page.second.texture.setSmooth(smooth);
        }
    }
}

//private
Glyph Font::loadGlyph(std::uint32_t codepoint, std::uint32_t charSize, bool bold, float outlineThickness) const
{
    Glyph retVal;

    if (!m_face.has_value())
    {
        return retVal;
    }

    auto face = std::any_cast<FT_Face>(m_face);
    if (!face)
    {
        return retVal;
    }

    if (!setCurrentCharacterSize(charSize))
    {
        return retVal;
    }

    //fetch the glyph
    FT_Int32 flags = FT_LOAD_TARGET_NORMAL | FT_LOAD_FORCE_AUTOHINT;

    if (FT_Load_Char(face, codepoint, flags) != 0)
    {
        return retVal;
    }

    FT_Glyph glyphDesc;
    if (FT_Get_Glyph(face->glyph, &glyphDesc) != 0)
    {
        return retVal;
    }

    //get the outline
    FT_Pos weight = (1 << 6);
    bool outline = (glyphDesc->format == FT_GLYPH_FORMAT_OUTLINE);
    if (outline)
    {
        if (bold)
        {
            FT_OutlineGlyph outlineGlyph = (FT_OutlineGlyph)glyphDesc;
            FT_Outline_Embolden(&outlineGlyph->outline, weight);
        }

        if (outlineThickness != 0)
        {
            auto stroker = std::any_cast<FT_Stroker>(m_stroker);
            FT_Stroker_Set(stroker, static_cast<FT_Fixed>(outlineThickness * MagicNumber), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
            FT_Glyph_Stroke(&glyphDesc, stroker, true);
        }
    }


    //rasterise it
    FT_Glyph_To_Bitmap(&glyphDesc, FT_RENDER_MODE_NORMAL, 0, 1);
    FT_Bitmap& bitmap = reinterpret_cast<FT_BitmapGlyph>(glyphDesc)->bitmap;

    if (!outline)
    {
        if (bold)
        {
            FT_Bitmap_Embolden(std::any_cast<FT_Library>(m_library), &bitmap, weight, weight);
        }
    }


    retVal.advance = static_cast<float>(face->glyph->metrics.horiAdvance) / static_cast<float>(1<<6);
    if (bold)
    {
        retVal.advance += static_cast<float>(weight) / MagicNumber; //surely this is the same as adding 1?
    }

    std::int32_t width = bitmap.width;
    std::int32_t height = bitmap.rows;

    if (width > 0 && height > 0)
    {
        const std::uint32_t padding = 2;

        //pad the glyph to stop potential bleed
        width += 2 * padding;
        height += 2 * padding;

        //get the current page
        auto& page = m_pages[charSize];
        page.texture.setSmooth(m_useSmoothing);

        //find somewhere to insert the glyph
        retVal.textureBounds = getGlyphRect(page, width, height);

        //readjust texture rect for padding
        retVal.textureBounds.left += padding;
        retVal.textureBounds.bottom += padding;
        retVal.textureBounds.width -= padding * 2;
        retVal.textureBounds.height -= padding * 2;

        retVal.bounds.left = static_cast<float>(face->glyph->metrics.horiBearingX) / MagicNumber;
        retVal.bounds.bottom = static_cast<float>(face->glyph->metrics.horiBearingY) / MagicNumber;
        retVal.bounds.width = static_cast<float>(face->glyph->metrics.width) / MagicNumber;
        retVal.bounds.height = static_cast<float>(face->glyph->metrics.height) / MagicNumber;
        retVal.bounds.bottom -= retVal.bounds.height;

        //buffer the pixel data and update the page texture
        m_pixelBuffer.resize(width * height * 4);

        auto* current = m_pixelBuffer.data();
        auto* end = current + m_pixelBuffer.size();

        while (current != end)
        {
            (*current++) = 255;
            (*current++) = 255;
            (*current++) = 255;
            (*current++) = 0;
        }

        //copy from rasterised bitmap
        const auto* pixels = bitmap.buffer;
        if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
        {
            //for(auto y = height - padding - 1; y >= padding; --y)
            for(auto y = padding; y < height - padding; ++y)
            {
                for (auto x = padding; x < width - padding; ++x)
                {
                    std::size_t index = x + y * width;
                    m_pixelBuffer[index * 4 + 3] = ((pixels[(x - padding) / 8]) & (1 << (7 - ((x - padding) % 8)))) ? 255 : 0;
                }
                pixels += bitmap.pitch;
            }
        }
        else
        {
            //for (auto y = height - padding - 1; y >= padding; --y)
            for (auto y = padding; y < height - padding; ++y)
            {
                for (auto x = padding; x < width - padding; ++x)
                {
                    std::size_t index = x + y * width;
                    m_pixelBuffer[index * 4 + 3] = pixels[x - padding];
                }
                pixels += bitmap.pitch;
            }
        }

        //finally copy to texture
        auto x = static_cast<std::uint32_t>(retVal.textureBounds.left) - padding;
        auto y = static_cast<std::uint32_t>(retVal.textureBounds.bottom) - padding;
        auto w = static_cast<std::uint32_t>(retVal.textureBounds.width) + padding * 2;
        auto h = static_cast<std::uint32_t>(retVal.textureBounds.height) + padding * 2;
        page.texture.update(m_pixelBuffer.data(), false, { x,y,w,h });
    }

    return retVal;
}

FloatRect Font::getGlyphRect(Page& page, std::uint32_t width, std::uint32_t height) const
{
    Row* row = nullptr;
    float bestRatio = 0.f;

    for (auto& currRow : page.rows)
    {
        float ratio = static_cast<float>(height) / currRow.height;

        //ignore rows too short or too tall
        if (ratio < 0.7f || ratio > 1.f)
        {
            continue;
        }

        //check there's space in the row
        if (width > page.texture.getSize().x - currRow.width)
        {
            continue;
        }

        if (ratio < bestRatio)
        {
            continue;
        }

        row = &currRow;
        bestRatio = ratio;
        break;
    }

    //if we didn't find a row, insert one 10% bigger than the glyph
    if (!row)
    {
        std::int32_t rowHeight = height + (height / 10);
        while (page.nextRow + rowHeight > page.texture.getSize().y
            || width >= page.texture.getSize().x)
        {
            auto texWidth = page.texture.getSize().x;
            auto texHeight = page.texture.getSize().y;

            if (texWidth * 2 <= Texture::getMaxTextureSize()
                && texHeight * 2 <= Texture::getMaxTextureSize())
            {
                //increase texture 4 fold
                Texture texture;
                texture.create(texWidth * 2, texHeight * 2);
                texture.setSmooth(true);
                texture.update(page.texture);
                page.texture.swap(texture);
                page.updated = true;
            }
            else
            {
                //doesn't fit :(
                Logger::log("Failed to add new character to font - max texture size reached.", Logger::Type::Error);
                return { 0.f, 0.f, 2.f, 2.f };
            }
        }

        row = &page.rows.emplace_back(page.nextRow, rowHeight);
        page.nextRow += rowHeight;
    }

    FloatRect retVal(static_cast<float>(row->width), static_cast<float>(row->top), static_cast<float>(width), static_cast<float>(height));
    row->width += width;

    return retVal;
}

bool Font::setCurrentCharacterSize(std::uint32_t size) const
{
    auto face = std::any_cast<FT_Face>(m_face);
    FT_UShort currentSize = face->size->metrics.x_ppem;

    if (currentSize != size)
    {
        FT_Error result = FT_Set_Pixel_Sizes(face, 0, size);

        if (result == FT_Err_Invalid_Pixel_Size)
        {
            Logger::log("Failed to set font face to " + std::to_string(size), Logger::Type::Error);

            //bitmap fonts may fail if the size isn't supported
            if (!FT_IS_SCALABLE(face))
            {
                Logger::log("Available sizes:", Logger::Type::Info);
                std::stringstream ss;
                for (auto i = 0; i < face->num_fixed_sizes; ++i)
                {
                    ss << ((face->available_sizes[i].y_ppem + 32) >> 6) << " ";
                }
                Logger::log(ss.str(), Logger::Type::Info);
            }
        }

        return result == FT_Err_Ok;
    }
    return true;
}

void Font::cleanup()
{
    if (m_stroker.has_value())
    {
        auto stroker = std::any_cast<FT_Stroker>(m_stroker);
        if (stroker)
        {
            FT_Stroker_Done(stroker);
        }
    }

    if (m_face.has_value())
    {
        auto face = std::any_cast<FT_Face>(m_face);
        if (face)
        {
            FT_Done_Face(face);
        }
    }

    if (m_library.has_value())
    {
        auto library = std::any_cast<FT_Library>(m_library);
        if (library)
        {
            FT_Done_FreeType(library);
        }
    }

    m_stroker.reset();
    m_face.reset();
    m_library.reset();

    m_pages.clear();
    m_pixelBuffer.clear();
}

bool Font::pageUpdated(std::uint32_t charSize) const
{
    return m_pages[charSize].updated;
}

void Font::markPageRead(std::uint32_t charSize) const
{
    m_pages[charSize].updated = false;
}

Font::Page::Page()
{
    cro::Image img;
    img.create(128, 128, Colour(1.f, 1.f, 1.f, 0.f));
    texture.create(128, 128);
    texture.update(img.getPixelData());
}