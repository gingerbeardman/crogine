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

#include "HoleData.hpp"
#include "Billboard.hpp"
#include "Treeset.hpp"

#include <crogine/gui/GuiClient.hpp>
#include <crogine/ecs/Entity.hpp>
#include <crogine/ecs/components/BillboardCollection.hpp>
#include <crogine/graphics/MeshData.hpp>
#include <crogine/graphics/Image.hpp>
#include <crogine/graphics/RenderTexture.hpp>

#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <array>

namespace cro
{
    struct ResourceCollection;
    class Scene;
    class Image;
}

struct ThemeSettings final
{
    std::string billboardModel;
    std::string billboardSprites;
    //cro::Colour grassColour = cro::Colour(0.1568f, 0.305f, 0.2627f, 1.f);
    cro::Colour grassColour = cro::Colour(0.137f, 0.274f, 0.231f, 1.f);
    cro::Colour grassTint = cro::Colour(0.123f, 0.246f, 0.207f, 1.f);
    std::string instancePath;
    std::string cloudPath;

    static constexpr std::size_t MaxTreeSets = 4;
    std::vector<Treeset> treesets;
};

struct SharedStateData;
class TerrainBuilder final : public cro::GuiClient
{
public:
    TerrainBuilder(SharedStateData&, const std::vector<HoleData>&);
    ~TerrainBuilder();

    TerrainBuilder(const TerrainBuilder&) = delete;
    TerrainBuilder(TerrainBuilder&&) = delete;
    TerrainBuilder& operator = (const TerrainBuilder&) = delete;
    TerrainBuilder& operator = (TerrainBuilder&&) = delete;


    void create(cro::ResourceCollection&, cro::Scene&, const ThemeSettings&); //initial creation

    void update(std::size_t); //loads the configured data into the existing scene and signals the thread to queue upcoming data

    void setSlopePosition(glm::vec3);

private:
    SharedStateData& m_sharedData;
    const std::vector<HoleData>& m_holeData;
    std::size_t m_currentHole;

    std::array<cro::Billboard, BillboardID::Count> m_billboardTemplates = {};
    std::vector<cro::Billboard> m_billboardBuffer;
    std::array<cro::Entity, 2u> m_billboardEntities = {};
    std::size_t m_swapIndex; //might not swap every hole so we need to track this independently

    std::vector<glm::mat4> m_instanceTransforms;
    std::array<cro::Entity, 2u> m_instancedEntities = {};

    static constexpr std::size_t MaxShrubInstances = 4;
    std::array<std::vector<glm::mat4>, MaxShrubInstances> m_shrubTransforms;
    std::array<std::array<cro::Entity, MaxShrubInstances>, 2u> m_instancedShrubs = {};


    std::array<std::vector<cro::Entity>, 2u> m_crowdEntities = {};

    struct TerrainVertex final
    {
        glm::vec3 position = glm::vec3(0.f);
        //glm::vec4 colour = glm::vec4(0.1f, 0.117f, 0.176f, 1.f);
        glm::vec4 colour = glm::vec4(0.1568f, 0.305f, 0.2627f, 1.f);
        glm::vec3 normal = glm::vec3(0.f, 1.f, 0.f);

        //these actually get attached to tan/bitan attribs in the shader
        //but we'll use them as morph targets
        glm::vec3 targetPosition = glm::vec3(0.f);
        glm::vec3 targetNormal = glm::vec3(0.f, 1.f, 0.f);
    };
    std::vector<TerrainVertex> m_terrainBuffer;

    struct TerrainProperties final
    {
        std::uint32_t vbo = 0;
        std::uint32_t shaderID = 0;
        std::int32_t morphUniform = -1;
        float morphTime = 0.f;
    }m_terrainProperties;
    cro::Entity m_terrainEntity;


    struct SlopeVertex final
    {
        glm::vec3 position = glm::vec3(0.f);
        glm::vec4 colour = glm::vec4(1.f);
        glm::vec3 normal = glm::vec3(0.f);
        glm::vec2 texCoord = glm::vec2(0.f);
    };
    std::vector<SlopeVertex> m_slopeBuffer;
    std::vector<std::uint32_t> m_slopeIndices;
    struct SlopeProperties final
    {
        cro::Mesh::Data* meshData = nullptr;
        std::int32_t positionUniform = -1;
        std::int32_t alphaUniform = -1;
        std::uint32_t shader = 0;
        cro::Entity entity;
    }m_slopeProperties;
    std::vector<glm::vec3> m_normalMapBuffer;


    std::atomic_bool m_threadRunning;
    std::atomic_bool m_wantsUpdate;
    std::unique_ptr<std::thread> m_thread;

    void threadFunc();

    cro::RenderTexture m_normalMap;
    cro::Shader m_normalShader;
    cro::Image m_normalMapImage;
    struct HoleHeight final
    {
        float bottom = 0.f;
        float height = 0.f;
    }m_holeHeight;
    void renderNormalMap(); //don't call this from thread!!

#ifdef CRO_DEBUG_
    cro::Texture m_normalDebugTexture;
#endif 
};