/*-----------------------------------------------------------------------

Matt Marchant 2021 - 2022
http://trederia.blogspot.com

Super Video Golf - zlib licence.

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

#include <string>

static const std::string MinimapVertex = R"(
        uniform mat4 u_worldViewMatrix;
        uniform mat4 u_projectionMatrix;

        ATTRIBUTE vec2 a_position;
        ATTRIBUTE MED vec2 a_texCoord0;
        ATTRIBUTE LOW vec4 a_colour;

        VARYING_OUT LOW vec4 v_colour;
        VARYING_OUT MED vec2 v_texCoord;

        void main()
        {
            gl_Position = u_projectionMatrix * u_worldViewMatrix * vec4(a_position, 0.0, 1.0);
            v_colour = a_colour;
            v_texCoord = a_texCoord0;
        })";

//minimap as in top down view of green
static const std::string MinimapFragment = R"(
        uniform sampler2D u_texture;

        VARYING_IN LOW vec4 v_colour;
        VARYING_IN MED vec2 v_texCoord;
        OUTPUT
        
        const float stepPos = (0.44 * 0.44);
        const float borderPos = (0.48 * 0.48);

        const vec3 borderColour = vec3(0.314,0.157,0.184);

//these ought to be uniforms for texture
//res and screen scale
const float res = 100.0;// 66.0;
const float scale = 2.0;

        void main()
        {
            vec2 pos = (round(floor(v_texCoord * res) * scale) / scale) / res;

            vec2 dir = pos - vec2(0.5);
            float length2 = dot(dir,dir);

            FRAG_OUT = TEXTURE(u_texture, v_texCoord) * v_colour;
            //FRAG_OUT.rgb = mix(FRAG_OUT.rgb, borderColour, step(borderPos, length2));
            FRAG_OUT.a = 1.0 - step(stepPos, length2);
        })";

//minimap as in mini course view
//well, this is a silly inconsistency...
static const std::string MinimapViewVertex = R"(
        uniform mat4 u_worldMatrix;
        uniform mat4 u_projectionMatrix;

        ATTRIBUTE vec2 a_position;
        ATTRIBUTE MED vec2 a_texCoord0;
        ATTRIBUTE LOW vec4 a_colour;

        VARYING_OUT LOW vec4 v_colour;
        VARYING_OUT MED vec2 v_texCoord;

        void main()
        {
            gl_Position = u_projectionMatrix * u_worldMatrix * vec4(a_position, 0.0, 1.0);
            v_colour = a_colour;
            v_texCoord = a_texCoord0;
        })";

static const std::string MinimapViewFragment = R"(
        uniform sampler2D u_texture;

        VARYING_IN LOW vec4 v_colour;
        VARYING_IN MED vec2 v_texCoord;
        OUTPUT

        uniform float u_effect = 0.0;

#include BAYER_MATRIX

        const float RadiusOuter = (0.48 * 0.48);
        const float RadiusInner = (0.4 * 0.4);

        void main()
        {
            FRAG_OUT = TEXTURE(u_texture, v_texCoord) * v_colour;

            vec2 pos = v_texCoord - vec2(0.5);
            float len = dot(pos, pos);

            int x = int(mod(gl_FragCoord.x, MatrixSize));
            int y = int(mod(gl_FragCoord.y, MatrixSize));

            FRAG_OUT.a *= findClosest(x, y, 1.0 - (smoothstep(RadiusInner, RadiusOuter, len) * u_effect));
        })";