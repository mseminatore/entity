#include <stdio.h>
#include "entity.h"
#include "components.h"
#include <vector>

using EntityList = std::vector<Entity>;

//
void collisionSystem(EntityManager& entityManager, EntityList& entities)
{
	// Check for collisions between entities with position components
}

//
void movementSystem(EntityManager& entityManager, EntityList& entities)
{
	// Update all entities with a position and velocity component
	for (Entity entity : entities)
	{
		if (entityManager.isEntityAlive(entity))
		{
			Position& position = entityManager.getComponent<Position>(entity);
			Velocity& velocity = entityManager.getComponent<Velocity>(entity);
			position.x += velocity.vx;
			position.y += velocity.vy;
		}
	}
}

// update entities in the game world
void update(EntityManager& entityManager, EntityList& entities)
{
	// Update all entities
	movementSystem(entityManager, entities);
	collisionSystem(entityManager, entities);
}

void render(EntityManager& entityManager, EntityList& entities)
{
	// Render all entities with a position component
	for (Entity entity : entities)
	{
		if (entityManager.isEntityAlive(entity))
		{
			Position& position = entityManager.getComponent<Position>(entity);
			printf("Entity %llu: Position (%f, %f)\n", entity, position.x, position.y);
		}
	}
}

void handleInput(EntityManager& entityManager, EntityList& entities)
{
	// Handle user input and update entities accordingly
}

void gameLoop(EntityManager& entityManager, EntityList& entities)
{
	while (true)
	{
		handleInput(entityManager, entities);
		update(entityManager, entities);
		render(entityManager, entities);
	}
}

void initializeGame(EntityManager& entityManager, EntityList& entities)
{
	// Initialize game entities and components
	Entity rock = entityManager.create();
	entityManager.addComponent<Position>(rock, Position{ 10.0f, 0.0f });
	entityManager.addComponent<Radius>(rock, Radius{ 5.0f });
	entities.push_back(rock);

	Entity missile = entityManager.create();
	entityManager.addComponent<Position>(missile, Position{ 0.0f, 5.0f });
	entityManager.addComponent<Velocity>(missile, Velocity{ 1.0f, 0.0f });
	entityManager.addComponent<Radius>(missile, Radius{ 1.0f });
	entities.push_back(missile);
}

// main entry point for the application
int main(int argc, char *argv[])
{
	EntityManager entityManager;
	EntityList entities;

	initializeGame(entityManager, entities);

	gameLoop(entityManager, entities);

    return 0; 
}
