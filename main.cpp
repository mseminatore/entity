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
	std::vector<Entity> toDestroy;

	// Check for collisions between entities with position components
	entityManager.query<Position, Radius, Name>().for_each([&entityManager](Position& p1, Radius& r1, Name& name1) {
		entityManager.query<Position, Velocity, Radius, Name>().for_each([&](Position& p2, Velocity& v, Radius& r2, Name& name2) {
			if (&p1 == &p2) return; // skip self
			float dx = p1.x - p2.x;
			float dy = p1.y - p2.y;
			float distanceSquared = dx * dx + dy * dy;
			float radiusSum = r1.r + r2.r;
			if (distanceSquared < radiusSum * radiusSum) {
				printf("Collision detected between %s and %s!\n", name1.value.c_str(), name2.value.c_str());
			}
		});
	});

	for (Entity e : toDestroy) {
		if (entityManager.isEntityAlive(e))
			entityManager.destroy(e);
	}
}

//--------------------------------------------------------------------------------------------
// update position of entities based on their velocity components
//--------------------------------------------------------------------------------------------
void movementSystem(EntityManager& entityManager)
{
	// Update all entities with a position and velocity component
	entityManager.query<Position, Velocity>().for_each([](Position& p, Velocity& v) {
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
	entityManager.query<Name, Position>().for_each([](Name &name, Position& p) {
		printf("Rendering entity %s at position (%f, %f)\n", name.value.c_str(), p.x, p.y);
	});
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void handleInput(EntityManager& entityManager)
{
	// Handle user input and update entities accordingly
	getchar(); // Wait for user input to proceed to the next frame
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void gameLoop(EntityManager& entityManager)
{
	while (true)
	{
		handleInput(entityManager);
		update(entityManager);
		render(entityManager);
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
	EntityList entities;

	initializeGame(entityManager);

	gameLoop(entityManager);

    return 0; 
}
