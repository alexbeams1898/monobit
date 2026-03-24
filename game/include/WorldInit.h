#pragma once

class Engine;
class EntityManager;

namespace WorldInit
{

void createWorld(Engine& engine, EntityManager& em);
void destroyWorld(EntityManager& em);

} // namespace WorldInit
