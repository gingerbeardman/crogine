/*-----------------------------------------------------------------------

Matt Marchant 2021
http://trederia.blogspot.com

crogine application - Zlib license.

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

#include <crogine/graphics/Rectangle.hpp>

#include <string>
#include <vector>

class MapData final
{
public:
    MapData();

    bool loadFromFile(const std::string&, bool binary = false);

    const std::vector<cro::FloatRect>& getCollisionRects(std::size_t layer) const { return m_collisionRects[layer]; }
    const std::vector<cro::FloatRect>& getTeleportRects(std::size_t layer) const { return m_teleportRects[layer]; }
    const std::vector<glm::vec2>& getCratePositions(std::size_t layer) const { return m_crateSpawns[layer]; }
    const std::vector<glm::vec2>& getSpawnPositions() const { return m_playerSpawns; }

private:
    std::array<std::vector<cro::FloatRect>, 2u> m_collisionRects;
    std::array<std::vector<cro::FloatRect>, 2u> m_teleportRects;
    std::array<std::vector<glm::vec2>, 2u> m_crateSpawns;
    std::vector<glm::vec2> m_playerSpawns;
};