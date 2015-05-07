/*TODO:

Clean Up
---------
- Free the tile texture atlas when loading a new level
- clean up text entity, texture memory

- Better loading in of entities from the tmx file

- re-enable laser bolt player, collision


Bug Fixes
----------
- Fix flickering bug (need proper subpixel accuracy when blitting sprites)
- Fix entity position on bigger screen sizes (when loading in from .tmx)
- when patrolling change all of the hitboxes, not just the first one
- test remove when outside level to see that it works
- if the entity which you are keyboard controlling is removed, the level should restart

- If the text becomes keyboard controlled when inside an entity, ignore collisions with it until it gets outside


New Features
-------------
- locking fields so they can't be moved
- locking fields so they can't be modified

- Fade console fields in and out

- Multiline text

- use a priority queue to process path requests

- Trail effect on death

- Cloning entire entities 

- When an entity dies, make its fields collectable in the world so they don't just disappear

- Handle shadows properly

- Hacking gravity
- Hack the topology of the world
- Hack to make player reflect bullets
- Hack the mass of entities

- Make the background hackable (change from sunset to marine)
- Hack the text and type in new messages

- Make entities resizable
- Line of sight for enemies

*/

#define arrayCount(array) sizeof(array) / sizeof(array[0])
#define InvalidCodePath assert(!"Invalid Code Path")
#define InvalidDefaultCase default: { assert(!"Invalid Default Case"); } break

#define SHOW_COLLISION_BOUNDS 0
#define SHOW_CLICK_BOUNDS 0

#include <stdint.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef int32_t bool32; 
typedef int16_t bool16; 
typedef int8_t bool8; 

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cassert>
#include <string>

#include "glew.h"

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"
#include "SDL_opengl.h"

#include "hackformer_math.h"
#include "hackformer_renderer.h"
#include "hackformer_consoleField.h"
#include "hackformer_entity.h"

struct Key {
	bool32 pressed;
	bool32 justPressed;
	s32 keyCode1, keyCode2;
};

struct Input {
	V2 mouseInPixels;
	V2 mouseInMeters;
	V2 mouseInWorld;

	//TODO: There might be a bug when this, it was noticeable when shift was held down 
	//	 	while dragging something with the mouse
	V2 dMouseMeters;

	union {
		//NOTE: The number of keys in the array must always be equal to the number of keys in the struct below
		Key keys[8];

		struct {
			Key up;
			Key down;
			Key left;
			Key right;
			Key r;
			Key x;
			Key shift;
			Key leftMouse;
		};
	};
};

struct MemoryArena {
	void* base;
	size_t allocated;
	size_t size;
};

struct PathNode {
	bool32 solid;
	bool32 open;
	bool32 closed;
	PathNode* parent;
	double costToHere;
	double costToGoal;
	V2 p;
	s32 tileX, tileY;
};

struct EntityChunk {
	s32 entityRefs[16];
	s32 numRefs;
	EntityChunk* next;
};

struct GameState {
	Entity entities[1000];
	s32 numEntities;

	//NOTE: 0 is the null reference
	EntityReference entityRefs_[500];
	

	//NOTE: These must be sequential for laser collisions to work
	s32 refCount;

	MemoryArena permanentStorage;
	MemoryArena levelStorage;

	RenderGroup* renderGroup;
	SDL_Renderer* renderer;
	TTF_Font* textFont;
	CachedFont consoleFont;

	Input input;
	V2 cameraP;
	V2 newCameraP;

	RefNode* targetRefs;
	s32 consoleEntityRef;
	s32 playerRef;
	s32 playerDeathRef;

	bool32 loadNextLevel;
	bool32 doingInitialSim;

	ConsoleField* consoleFreeList;
	RefNode* refNodeFreeList;
	EntityReference* entityRefFreeList;
	Hitbox* hitboxFreeList;

	ConsoleField* swapField;
	V2 swapFieldP;

	PathNode* openPathNodes[10000];
	s32 openPathNodesCount;
	PathNode* solidGrid;
	s32 solidGridWidth, solidGridHeight;
	double solidGridSquareSize;

	EntityChunk* chunks;
	s32 chunksWidth, chunksHeight;
	V2 chunkSize;

	s32 blueEnergy;

	double shootDelay;
	double tileSize;
	V2 mapSize;
	V2 worldSize;

	double pixelsPerMeter;
	s32 windowWidth, windowHeight;
	V2 windowSize;

	V2 gravity;
	V2 texel;

	V2 playerStartP;
	V2 playerDeathStartP;
	V2 playerDeathSize;

	AnimNode playerStand;
	AnimNode playerWalk;
	AnimNode playerHack;
	AnimNode playerJump;

	AnimNode virus1Stand;
	AnimNode virus1Shoot;

	AnimNode flyingVirusStand;
	AnimNode flyingVirusShoot;

	Texture bgTex, mgTex;
	Texture sunsetCityBg, sunsetCityMg;
	Texture marineCityBg, marineCityMg;

	Texture blueEnergyTex;
	Texture laserBolt;
	Texture endPortal;

	Texture consoleTriangle, consoleTriangleSelected, consoleTriangleGrey, consoleTriangleYellow;

	Texture laserBaseOff, laserBaseOn;
	Texture laserTopOff, laserTopOn;
	Texture laserBeam;

	Texture heavyTileTex;
	Texture* tileAtlas;

	Texture dock;
	Texture dockBlueEnergyTile;
	Texture attribute, behaviour;
};

#define pushArray(arena, type, count) (type*)pushIntoArena_(arena, count * sizeof(type))
#define pushStruct(arena, type) (type*)pushIntoArena_(arena, sizeof(type))
#define pushSize(arena, size) pushIntoArena_(arena, size);
void* pushIntoArena_(MemoryArena* arena, size_t amt) {
	arena->allocated += amt;
	assert(arena->allocated < arena->size);

	void* result = (char*)arena->base + arena->allocated - amt;
	return result;
}

void zeroSize(void* base, size_t size) {
	for (size_t byteIndex = 0; byteIndex < size; byteIndex++) {
		*((char*)base + byteIndex) = 0;
	}
}
