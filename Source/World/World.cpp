#include "World.h"

#include "../Renderer/RenderMaster.h"

#include <iostream>

#include "../Maths/Vector2XZ.h"
#include "../Camera.h"
#include "../ToggleKey.h"


World::World(const Camera& camera, const Config& config)
:   m_chunkManager      (*this)
,   m_renderDistance    (config.renderDistance)
{
    for (int i = 0; i < 2; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        m_chunkLoadThreads.emplace_back([&]()
        {
            loadChunks(camera);
        });
    }

}

World::~World()
{
    m_isRunning = false;
    for (auto& thread : m_chunkLoadThreads)
    {
        thread.join();
    }
}

//world coords into chunk column coords
ChunkBlock World::getBlock(int x, int y, int z)
{
    auto bp = getBlockXZ(x, z);
    auto chunkPosition = getChunkXZ(x, z);

    return m_chunkManager.getChunk(chunkPosition.x, chunkPosition.z).getBlock(bp.x, y, bp.z);
}

void World::setBlock(int x, int y, int z, ChunkBlock block)
{
    if (y <= 0)
        return;

    auto bp = getBlockXZ(x, z);
    auto chunkPosition = getChunkXZ(x, z);

    m_chunkManager.getChunk(chunkPosition.x, chunkPosition.z).setBlock(bp.x, y, bp.z, block);
}

//loads chunks
//make chunk meshes
void World::update(const Camera& camera)
{
    static ToggleKey key(sf::Keyboard::C);

    if (key.isKeyPressed())
    {
        m_mutex.lock();
        m_chunkManager.deleteMeshes();
        m_loadDistance = 2;
        m_mutex.unlock();
    }


    for (auto& event : m_events)
    {
        event->handle(*this);
    }
    m_events.clear();

    updateChunks();
}

///@TODO
///Optimize for chunkPositionU usage :thinking:
void World::loadChunks(const Camera& camera)
{
    while(m_isRunning)
    {
        bool isMeshMade = false;
        int cameraX = camera.position.x / CHUNK_SIZE;
        int cameraZ = camera.position.z / CHUNK_SIZE;

        for (int i = 0; i < m_loadDistance; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            int minX = std::max(cameraX  - i, 0);
            int minZ = std::max(cameraZ  - i, 0);
            int maxX = cameraX + i;
            int maxZ = cameraZ + i;

            for (int x = minX; x < maxX; ++x)
            {
                for (int z = minZ; z < maxZ; ++z)
                {
                    m_mutex.lock();
                    isMeshMade = m_chunkManager.makeMesh(x, z, camera);
                    m_mutex.unlock();
                }
                if (isMeshMade)
                    break;
            }

            if (isMeshMade)
                break;
        }

        if (!isMeshMade)
        {
            m_loadDistance++;
        }
        if (m_loadDistance >= m_renderDistance)
        {
            m_loadDistance = 2;
        }
    }
}


void World::updateChunk(int blockX, int blockY, int blockZ)
{
    m_mutex.lock();

    auto addChunkToUpdateBatch = [&](const sf::Vector3i& key, ChunkSection& section)
    {
        m_chunkUpdates.emplace(key, &section);
    };

    auto chunkPosition = getChunkXZ(blockX, blockZ);
    auto chunkSectionY = blockY / CHUNK_SIZE;

    sf::Vector3i key(chunkPosition.x, chunkSectionY, chunkPosition.z);
    addChunkToUpdateBatch(key, m_chunkManager.getChunk(chunkPosition.x, chunkPosition.z).getSection(chunkSectionY));

    auto sectionBlockXZ = getBlockXZ(blockX, blockZ);
    auto sectionBlockY  = blockY % CHUNK_SIZE;

    if (sectionBlockXZ.x == 0)
    {
        sf::Vector3i newKey(chunkPosition.x - 1, chunkSectionY, chunkPosition.z);
        addChunkToUpdateBatch(newKey, m_chunkManager.getChunk(newKey.x, newKey.z).getSection(newKey.y));
    }
    else if (sectionBlockXZ.x == CHUNK_SIZE - 1)
    {
        sf::Vector3i newKey(chunkPosition.x + 1, chunkSectionY, chunkPosition.z);
        addChunkToUpdateBatch(newKey, m_chunkManager.getChunk(newKey.x, newKey.z).getSection(newKey.y));
    }

    if (sectionBlockY == 0)
    {
        sf::Vector3i newKey(chunkPosition.x, chunkSectionY - 1, chunkPosition.z);
        addChunkToUpdateBatch(newKey, m_chunkManager.getChunk(newKey.x, newKey.z).getSection(newKey.y));
    }
    else if (sectionBlockY == CHUNK_SIZE - 1)
    {
        sf::Vector3i newKey(chunkPosition.x, chunkSectionY + 1, chunkPosition.z);
        addChunkToUpdateBatch(newKey, m_chunkManager.getChunk(newKey.x, newKey.z).getSection(newKey.y));
    }

    if (sectionBlockXZ.z == 0)
    {
        sf::Vector3i newKey(chunkPosition.x, chunkSectionY, chunkPosition.z - 1);
        addChunkToUpdateBatch(newKey, m_chunkManager.getChunk(newKey.x, newKey.z).getSection(newKey.y));
    }
    else if (sectionBlockXZ.z == CHUNK_SIZE - 1)
    {
        sf::Vector3i newKey(chunkPosition.x, chunkSectionY, chunkPosition.z + 1);
        addChunkToUpdateBatch(newKey, m_chunkManager.getChunk(newKey.x, newKey.z).getSection(newKey.y));
    }

    m_mutex.unlock();
}

void World::renderWorld(RenderMaster& renderer, const Camera& camera)
{
    m_mutex.lock();
    renderer.drawSky();

    auto& chunkMap = m_chunkManager.getChunks();
    for (auto itr = chunkMap.begin(); itr != chunkMap.end();)
    {
        Chunk& chunk = itr->second;

        int cameraX = camera.position.x;
        int cameraZ = camera.position.z;

        int minX = (cameraX / CHUNK_SIZE) - m_renderDistance;
        int minZ = (cameraZ / CHUNK_SIZE) - m_renderDistance;
        int maxX = (cameraX / CHUNK_SIZE) + m_renderDistance;
        int maxZ = (cameraZ / CHUNK_SIZE) + m_renderDistance;

        auto location = chunk.getLocation();

        if (minX > location.x ||
            minZ > location.y ||
            maxZ < location.y ||
            maxX < location.x)
        {
            itr = chunkMap.erase(itr);
            continue;
        }
        else
        {
            chunk.drawChunks(renderer, camera);
            itr++;
        }
    }
    m_mutex.unlock();
}

ChunkManager& World::getChunkManager()
{
    return m_chunkManager;
}

VectorXZ World::getBlockXZ(int x, int z)
{
    return
    {
        x % CHUNK_SIZE,
        z % CHUNK_SIZE
    };
}

VectorXZ World::getChunkXZ(int x, int z)
{
    return
    {
        x / CHUNK_SIZE,
        z / CHUNK_SIZE
    };
}

void World::updateChunks()
{
    m_mutex.lock();
    for (auto& c : m_chunkUpdates)
    {
        ChunkSection& s = *c.second;
        s.makeMesh();
    }
    m_chunkUpdates.clear();
    m_mutex.unlock();
}

