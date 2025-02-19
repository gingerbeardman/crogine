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

#pragma once

#include <crogine/Config.hpp>
#include <crogine/detail/Types.hpp>

#include <SDL_surface.h>

#include <vector>
#include <cstdint>

namespace cro
{
    namespace Detail
    {
        class DistanceField final
        {
        public:
            static std::vector<std::uint8_t> toDF(const SDL_Surface* input);

        private:
            static void twoD(std::vector<float>&, std::int32_t, std::int32_t);
            static std::vector<float> oneD(const std::vector<float>&, std::size_t);
            static std::vector<std::uint8_t> toBytes(const std::vector<float>&);
        };
    }
}