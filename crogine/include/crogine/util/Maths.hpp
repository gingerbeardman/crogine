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

#ifdef _MSC_VER
#define NOMINMAX
#endif //_MSC_VER

#include "Constants.hpp"
#include <crogine/util/Spline.hpp>

#include <type_traits>
#include <algorithm>

namespace cro
{
    namespace Util
    {
        namespace Maths
        {
            template <typename T>
            T clamp(T value, T min, T max)
            {
                static_assert(std::is_pod<T>::value, "Only available on POD");
                return std::min(max, std::max(min, value));
            }

            /*!
            \brief finds the shortest rotation, in radians, between the given start and end angles
            */
            static inline float shortestRotation(float start, float end)
            {
                float diff = end - start;
                if (diff > Const::PI) diff -= Const::TAU;
                else if (diff < -Const::PI) diff += Const::TAU;
                return diff;
            }

            /*!
            \brief Returns the normalised sign of the given value
            \param an int convertible value
            \returns -1 if the given value is negative, 1 if it is positive or 0
            if it has no value.
            */
            template <typename T>
            static inline std::int32_t sgn(T val)
            {
                static_assert(std::is_convertible<T, std::int32_t>::value);
                return (T(0) < val) - (val < T(0));
            }
        }
    }
}