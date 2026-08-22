#include <stdio.h>
#include "entity.h"
#include "components.h"
#include <vector>

using EntityList = std::vector<Entity>;

bool done = false;

//--------------------------------------------------------------------------------------------
// collision detection system 
//--------------------------------------------------------------------------------------------
void collisionSystem(EntityManager& entityManager)
{
	// we can't destroy entities while iterating over them, so we will store the entities to destroy in a separate vector
	std::vector<Entity> toDestroy;

	// N^2 check for collisions between entities with position components
	entityManager.view<Position, Radius, Name>().for_each([&entityManager, &toDestroy](Entity e1, Position& p1, Radius& r1, Name& name1) {
		entityManager.view<Position, Velocity, Radius, Name>().for_each([&](Entity e2, Position& p2, Velocity& v, Radius& r2, Name& name2) {
			if (e1 == e2)
				return; // skip self checks

			float dx = p1.x - p2.x;
			float dy = p1.y - p2.y;
			float distanceSquared = dx * dx + dy * dy;
			float radiusSum = r1.r + r2.r;
			if (distanceSquared < radiusSum * radiusSum) {
				printf("Collision detected between '%s' and '%s'!\n", name1.value.c_str(), name2.value.c_str());
				toDestroy.push_back(e2); // mark the second entity for destruction
			}
		});
	});

	// destroy all entities that were marked for destruction
	for (Entity e : toDestroy) {
		if (entityManager.isAlive(e)) {
			printf("Destroying entity '%s' (%llu) due to collision.\n", entityManager.get<Name>(e).value.c_str(), e);
			entityManager.destroy(e);
		}
	}
}

//--------------------------------------------------------------------------------------------
// update position of entities based on their velocity components
//--------------------------------------------------------------------------------------------
void movementSystem(EntityManager& entityManager)
{
	// Update all entities with a position and velocity component
	entityManager.view<Position, Velocity>().for_each([](Position& p, Velocity& v) {
		p.x += v.vx;
		p.y += v.vy;
	});
}

//--------------------------------------------------------------------------------------------
// update entities in the game world
//--------------------------------------------------------------------------------------------
void update(EntityManager& entityManager)
{
	// Update all entities
	movementSystem(entityManager);
	collisionSystem(entityManager);
}

//--------------------------------------------------------------------------------------------
// render system
//--------------------------------------------------------------------------------------------
void render(EntityManager& entityManager)
{
	entityManager.view<Name, Position>().for_each([](Name &name, Position& p) {
		printf("Rendering entity '%s' at position (%f, %f)\n", name.value.c_str(), p.x, p.y);
	});
}

constexpr int ESC_KEY = 27;

//--------------------------------------------------------------------------------------------
// handle user input
//--------------------------------------------------------------------------------------------
void handleInput(EntityManager& entityManager)
{
	// Handle user input and update entities accordingly
	int c = getchar(); // Wait for user input to proceed to the next frame

	if (c == ESC_KEY)
		done = true;
}

//--------------------------------------------------------------------------------------------
// main game loop
//--------------------------------------------------------------------------------------------
void gameLoop(EntityManager& entityManager)
{
	while (!done)
	{
		update(entityManager);
		render(entityManager);
		handleInput(entityManager);
	}
}

//--------------------------------------------------------------------------------------------
// setup of the game world and entities. could be loaded from a file or procedurally generated
//--------------------------------------------------------------------------------------------
void initializeGame(EntityManager& entityManager)
{
	// Initialize game entities and components
	Entity rock = entityManager.create();
	entityManager.add<Position>(rock, Position{ 10.0f, 5.0f });
	entityManager.add<Radius>(rock, Radius{ 5.0f });
	entityManager.add<Name>(rock, "Rock" );

	Entity missile = entityManager.create();
	entityManager.add<Position>(missile, Position{ 0.0f, 2.5f });
	entityManager.add<Velocity>(missile, Velocity{ 1.0f, 0.0f });
	entityManager.add<Radius>(missile, Radius{ 1.0f });
	entityManager.add<Name>(missile, "Missile" );
}

//--------------------------------------------------------------------------------------------
// main entry point for the application
//--------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	EntityManager entityManager;
	//EntityList entities;

	initializeGame(entityManager);

	gameLoop(entityManager);

    return 0; 
}
