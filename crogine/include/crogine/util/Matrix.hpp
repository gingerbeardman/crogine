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

#pragma once

#include <crogine/Config.hpp>

#include <crogine/detail/glm/vec3.hpp>
#include <crogine/detail/glm/mat4x4.hpp>

#include <utility>

namespace cro
{
    namespace Util
    {
        namespace Matrix
        {
            /*!
            \brief Returns the forward vector of the given matrix
            This won't be normalised if the scale is anything but 1,1,1
            */
            static inline glm::vec3 getForwardVector(const glm::mat4& mat)
            {
                return glm::vec3(-mat[2][0], -mat[2][1], -mat[2][2]);
            }

            /*!
            \brief Returns the up vector of the given matrix
            This won't be normalised if the scale is anything but 1,1,1
            */
            static inline glm::vec3 getUpVector(const glm::mat4& mat)
            {
                return glm::vec3(mat[1][0], mat[1][1], mat[1][2]);
            }

            /*!
            \brief Returns the right vector of the given matrix
            This won't be normalised if the scale is anything but 1,1,1
            */
            static inline glm::vec3 getRightVector(const glm::mat4& mat)
            {
                return glm::vec3(mat[0][0], mat[0][1], mat[0][2]);
            }

            /*!
            \brief Decomposes the given matrix into translation, rotation and scale components
            \param in Matrix to decompose
            \param outTranslation A reference to a vec 3 which will receive the translation values
            \param outRotation A reference to a quaternion which will receive the orientation values
            \param outScale A reference to a vec 3 which will receive the scale values
            */
            CRO_EXPORT_API bool decompose(const glm::mat4& in, glm::vec3& outTranslation, glm::quat& outRotation, glm::vec3& outScale);

            /*!
            \brief Returns a pair of floats containing the near and far plane values of the given projection matrix
            \param projMat Projection matrix
            \returns std::pair<float, float> Near and Far plane values
            */
            static inline std::pair<float, float> getNearFar(glm::mat4 projMat)
            {
                auto c = projMat[2][2];
                auto d = projMat[3][2];
                return { (d / (c - 1.f)), (d / (c + 1.f)) };
            }
        }
    }
}