//------------------------------------------------------
// Copyright (C) 2026 Mark Seminatore
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is furnished
// to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//------------------------------------------------------

#include <stdio.h>
#include <cinttypes>
#include "entity.h"
#include "components.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
		entityManager.view<Position, Velocity, Radius, Name>().for_each([&](Entity e2, Position& p2, Velocity&, Radius& r2, Name& name2) {
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

			if (auto name = entityManager.get<Name>(e)) {
				printf("Destroying entity '%s' (%" PRIu64 ") due to collision.\n", name->get().value.c_str(), e);
			}

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

constexpr int QUIT_KEY = 'q';

//--------------------------------------------------------------------------------------------
// handle user input
//--------------------------------------------------------------------------------------------
void handleInput(EntityManager&)
{
	// Handle user input and update entities accordingly
	int c = getchar(); // Wait for user input to proceed to the next frame

	if (c == QUIT_KEY)
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
// load entity definitions from a simple text file and construct them.
//
// File format: blank lines and lines starting with '#' are ignored. An
// "entity" line starts a new entity; each following line up to the next
// "entity" (or end of file) is "<ComponentName> <field values...>", e.g.:
//
//   entity
//   Position 10.0 5.0
//   Radius 5.0
//   Name Rock
//
// Only the component types below are recognized; add a case here whenever
// a new loadable component type is introduced.
//--------------------------------------------------------------------------------------------
void loadEntitiesFromFile(EntityManager& entityManager, const std::string& path)
{
	std::ifstream file(path);
	if (!file) {
		fprintf(stderr, "Could not open entity file '%s'\n", path.c_str());
		return;
	}

	Entity current = NullEntity;
	std::string line;

	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#')
			continue;

		std::istringstream iss(line);
		std::string keyword;
		iss >> keyword;

		if (keyword.empty())
			continue;

		if (keyword == "entity") {
			current = entityManager.create();
			continue;
		}

		if (current == NullEntity) {
			fprintf(stderr, "Component line before any 'entity' declaration: %s\n", line.c_str());
			continue;
		}

		if (keyword == "Position") {
			float x, y;
			iss >> x >> y;
			entityManager.add<Position>(current, Position{ x, y });
		} else if (keyword == "Velocity") {
			float vx, vy;
			iss >> vx >> vy;
			entityManager.add<Velocity>(current, Velocity{ vx, vy });
		} else if (keyword == "Radius") {
			float r;
			iss >> r;
			entityManager.add<Radius>(current, Radius{ r });
		} else if (keyword == "Health") {
			float hp;
			iss >> hp;
			entityManager.add<Health>(current, Health{ hp });
		} else if (keyword == "Bounds") {
			float width, height;
			iss >> width >> height;
			entityManager.add<Bounds>(current, Bounds{ width, height });
		} else if (keyword == "Name") {
			std::string name;
			std::getline(iss >> std::ws, name); // rest of the line, allowing spaces
			entityManager.add<Name>(current, name);
		} else {
			fprintf(stderr, "Unknown component '%s'\n", keyword.c_str());
		}
	}
}

//--------------------------------------------------------------------------------------------
// setup of the game world and entities, loaded from a data file
//--------------------------------------------------------------------------------------------
void initializeGame(EntityManager& entityManager, const std::string& entityFile)
{
	loadEntitiesFromFile(entityManager, entityFile);
}

//--------------------------------------------------------------------------------------------
// main entry point for the application
//--------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
	EntityManager entityManager;

	const char* entityFile = argc > 1 ? argv[1] : "examples/entities.txt";
	initializeGame(entityManager, entityFile);

	gameLoop(entityManager);

    return 0;
}
