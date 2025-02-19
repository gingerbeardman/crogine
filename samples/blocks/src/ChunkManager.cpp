/*
World Generation code based on Matthew Hopson's Open Builder
https://github.com/Hopson97/open-builder

MIT License

Copyright (c) 2019 Matthew Hopson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "ChunkManager.hpp"
#include "Voxel.hpp"
#include "WorldConsts.hpp"

#include <crogine/detail/Assert.hpp>

namespace
{
    //remember this might be negative if checking out of bounds
    //chunks!
    std::int32_t positionToIndex(glm::ivec3 pos)
    {
        if (pos.x < 0 || pos.x >= WorldConst::ChunksPerSide) return -1;
        if (pos.y < 0 || pos.y >= WorldConst::ChunksPerSide) return -1;
        if (pos.z < 0 || pos.z >= WorldConst::ChunksPerSide) return -1;

        return pos.x + WorldConst::ChunksPerSide * (pos.y + WorldConst::ChunksPerSide * pos.z);
    }
}

ChunkManager::ChunkManager()
    : m_errorChunk(*this, glm::ivec3(0))
{
    auto voxels = m_errorChunk.getVoxels();
    std::fill(voxels.begin(), voxels.end(), vx::OutOfBounds);
    m_errorChunk.setVoxels(voxels);

    //create a fixed area - this isn't going to be an infinite world...
    auto maxChunks = WorldConst::ChunksPerSide * WorldConst::ChunksPerSide * WorldConst::ChunksPerSide;
    for (auto z = 0; z < WorldConst::ChunksPerSide; ++z)
    {
        for (auto y = 0; y < WorldConst::ChunksPerSide; ++y)
        {
            for (auto x = 0; x < WorldConst::ChunksPerSide; ++x)
            {
                glm::ivec3 position(x, y, z);
                m_chunks.emplace_back(*this, position);
            }
        }
    }
}

//public
Chunk& ChunkManager::addChunk(glm::ivec3 position)
{
    //auto result = m_chunks.find(position);
    //if (result == m_chunks.end())
    //{
    //    m_chunkPositions.push_back(position);

    //    return m_chunks.emplace(
    //        std::piecewise_construct, std::forward_as_tuple(position), std::forward_as_tuple(*this, position)).first->second;
    //}
    //return result->second;

    auto idx = positionToIndex(position);
    CRO_ASSERT(idx >= 0 && idx < m_chunks.size(), "Index out of range");

    m_chunkPositions.push_back(position);
    return m_chunks[idx];
}

const Chunk& ChunkManager::getChunk(glm::ivec3 position) const
{
    //auto result = m_chunks.find(position);
    //if (result == m_chunks.end())
    //{
    //    return m_errorChunk;
    //}
    //return result->second;

    auto idx = positionToIndex(position);
    if (idx < 0 || idx >= m_chunks.size())
    {
        return m_errorChunk;
    }
    return m_chunks[idx];
}

std::uint8_t ChunkManager::getVoxel(glm::ivec3 position) const
{
    auto chunkPos = toChunkPosition(position);
    //auto result = m_chunks.find(chunkPos);

    //if (result == m_chunks.end())
    //{
    //    return vx::CommonType::OutOfBounds;
    //}
    //return result->second.getVoxelQ(toLocalVoxelPosition(position));

    return getChunk(chunkPos).getVoxelQ(toLocalVoxelPosition(position));
}

void ChunkManager::setVoxel(glm::ivec3 position, std::uint8_t id)
{
    auto chunkPos = toChunkPosition(position);
    //auto result = m_chunks.find(chunkPos);
    //auto local = toLocalVoxelPosition(position);

    //if (result != m_chunks.end())
    //{
    //    result->second.setVoxelQ(local, id);
    //}
    //else
    //{
    //    auto& chunk = addChunk(chunkPos);
    //    chunk.setVoxelQ(local, id);
    //}

    m_chunks[positionToIndex(chunkPos)].setVoxelQ(toLocalVoxelPosition(position), id);
    //ensureNeighbours(chunkPos);
}

bool ChunkManager::hasChunk(glm::ivec3 position) const
{
    //return (m_chunks.find(position) != m_chunks.end());
    auto idx = positionToIndex(position);
    return (idx >= 0 && idx < m_chunks.size()) && !m_chunks[idx].empty();
}

Manifold ChunkManager::collisionTest(glm::vec3 worldPos, cro::Box bounds) const
{
    auto voxelPos = toVoxelPosition(worldPos);
    auto worldBounds = bounds + worldPos;
    auto result = worldBounds;

    //get 9 below, 8 surrounding and 9 voxels above
    static const std::array<glm::ivec3, 26> offsetPositions =
    {
        glm::ivec3(-1,-1,-1), glm::ivec3(0,-1,-1), glm::ivec3(1,-1,-1),
        glm::ivec3(-1,-1, 0), glm::ivec3(0,-1, 0), glm::ivec3(1,-1, 0),
        glm::ivec3(-1,-1, 1), glm::ivec3(0,-1, 1), glm::ivec3(1,-1, 1),

        glm::ivec3(-1, 0,-1), glm::ivec3(0, 0,-1), glm::ivec3(1, 0,-1),
        glm::ivec3(-1, 0, 0),                      glm::ivec3(1, 0, 0),
        glm::ivec3(-1, 0, 1), glm::ivec3(0, 0, 1), glm::ivec3(1, 0, 1),

        glm::ivec3(-1, 1,-1), glm::ivec3(0, 1,-1), glm::ivec3(1, 1,-1),
        glm::ivec3(-1, 1, 0), glm::ivec3(0, 1, 0), glm::ivec3(1, 1, 0),
        glm::ivec3(-1, 1, 1), glm::ivec3(0, 1, 1), glm::ivec3(1, 1, 1)
    };


    //if we get these in order every time we can
    //make an assumption about the direction we're approaching
    //from to calculate the normal

    //for each ID check if it's solid, create an AABB if it is
    //then test / correct against input AABB
    static const cro::Box blockAABB(glm::vec3(0.f), glm::vec3(1.f));

    for (const auto& offset : offsetPositions)
    {
        auto testPos = offset + voxelPos;
        auto voxel = getVoxel(testPos);

        //TODO look up the type/collision in voxel manager
        //as we only want to collide with solid types (and track if collider is in water)
        if (voxel != 0 && voxel != vx::OutOfBounds)
        {
            auto voxelBox = blockAABB + testPos;
            cro::Box intersection;
            if (voxelBox.intersects(worldBounds, &intersection))
            {
                //solve collision by updating 'result'
                //TODO track surface normal for reflection of collider velocity
            }
        }
    }

    //return a manifold based on difference between input and output boxes
    Manifold retVal;
    auto diff = result[0] - worldBounds[0];
    auto length = glm::length(diff);
    if (length > 0)
    {
        retVal.normal = diff / length;
        retVal.penetration = length;
    }

    return retVal;
}