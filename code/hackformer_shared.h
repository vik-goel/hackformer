#define _CRT_SECURE_NO_WARNINGS

#define arrayCount(array) sizeof(array) / sizeof(array[0])
#define InvalidCodePath assert(!"Invalid Code Path")
#define InvalidDefaultCase default: { assert(!"Invalid Default Case"); } break

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
#include <cmath>
#include <cstdio>
#include <cassert>

#include "glew.h"

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"
#include "SDL_opengl.h"
#include "SDL_mixer.h"

#include "hackformer_math.h"

#define TEMP_PIXELS_PER_METER 70.0f
#define TILE_SIZE_IN_METERS 0.9f

#define TEXTURE_DATA_COUNT 200
#define ANIM_NODE_DATA_COUNT 20
#define CHARACTER_ANIM_DATA_COUNT 10

enum EntityType {
	EntityType_background,
	EntityType_player,
	EntityType_playerDeath,
	EntityType_tile,
	EntityType_heavyTile,
	EntityType_blueEnergy,
	EntityType_text,
	EntityType_virus,
	EntityType_laserBolt,
	EntityType_endPortal,
	EntityType_laserBase,
	EntityType_laserBeam,
	EntityType_flyingVirus,
	EntityType_pickupField,
};

enum DrawOrder {
	DrawOrder_background,
	DrawOrder_middleground,
	DrawOrder_text,
	DrawOrder_tile,
	DrawOrder_pickupField,
	DrawOrder_movingTile,
	DrawOrder_endPortal,
	DrawOrder_blueEnergy,
	DrawOrder_laserBeam,
	DrawOrder_laserBase,
	DrawOrder_virus,
	DrawOrder_flyingVirus,
	DrawOrder_heavyTile,
	DrawOrder_laserBolt,
	DrawOrder_playerDeath,
	DrawOrder_player,
	DrawOrder_gui,
};

struct MemoryArena {
	void* base;
	size_t allocated;
	size_t size;
};

MemoryArena createArena(size_t size, bool clearToZero) {
	MemoryArena result = {};
	result.size = size;

	if(clearToZero) {
		result.base = calloc(size, 1);
	} else {
		result.base = malloc(size);
	}

	assert(result.base);
	return result;
}

#define pushArray(arena, type, count) (type*)pushIntoArena_(arena, count * sizeof(type))
#define pushStruct(arena, type) (type*)pushIntoArena_(arena, sizeof(type))
#define pushSize(arena, size) pushIntoArena_(arena, size);
#define pushElem(arena, type, value) {void* valuePtr = pushIntoArena_(arena, sizeof(type)); *(type*)valuePtr = value;}

void* pushIntoArena_(MemoryArena* arena, size_t amt) {
	arena->allocated += amt;
	assert(arena->allocated < arena->size);

	void* result = (char*)arena->base + arena->allocated - amt;
	return result;
}

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
		Key keys[13];

		//TODO: The members of this struct may not be packed such that they align perfectly with the array of keys
		struct {
			Key up;
			Key down;
			Key left;
			Key right;
			Key r;
			Key x;
			Key n;
			Key m;
			Key shift;
			Key esc;
			Key pause;
			Key leftMouse;
			Key rightMouse;
		};
	};
};

struct Camera {
	V2 p;
	V2 newP;
	bool32 moveToTarget;
	bool32 deferredMoveToTarget;
	double scale;
};

void initInputKeyCodes(Input* input) {
	input->up.keyCode1 = SDLK_w;
	input->up.keyCode2 = SDLK_UP;

	input->down.keyCode1 = SDLK_s;
	input->down.keyCode2 = SDLK_DOWN;

	input->left.keyCode1 = SDLK_a;
	input->left.keyCode2 = SDLK_LEFT;

	input->right.keyCode1 = SDLK_d;
	input->right.keyCode2 = SDLK_RIGHT;

	input->r.keyCode1 = SDLK_r;
	input->x.keyCode1 = SDLK_x;
	input->shift.keyCode1 = SDLK_RSHIFT;
	input->shift.keyCode2 = SDLK_LSHIFT;

	input->pause.keyCode1 = SDLK_p;
	input->pause.keyCode2 = SDLK_ESCAPE;

	input->n.keyCode1 = SDLK_n;
	input->m.keyCode1 = SDLK_m;

	input->esc.keyCode1 = SDLK_ESCAPE;
}

void pollInput(Input* input, bool* running, double windowHeight, double pixelsPerMeter, V2 cameraP) {
	input->dMouseMeters = v2(0, 0);

	for(s32 keyIndex = 0; keyIndex < arrayCount(input->keys); keyIndex++) {
		Key* key = input->keys + keyIndex;
		key->justPressed = false;
	}

	SDL_Event event;
	while(SDL_PollEvent(&event) > 0) {
		switch(event.type) {
			case SDL_QUIT:
			*running = false;
			break;
			case SDL_KEYDOWN:
			case SDL_KEYUP: {
				bool pressed = event.key.state == SDL_PRESSED;
				SDL_Keycode keyCode = event.key.keysym.sym;

				for(s32 keyIndex = 0; keyIndex < arrayCount(input->keys); keyIndex++) {
					Key* key = input->keys + keyIndex;

					if(key->keyCode1 == keyCode || key->keyCode2 == keyCode) {
						if(pressed && !key->pressed) key->justPressed = true;
						key->pressed = pressed;
					}
				}
			} break;
			case SDL_MOUSEMOTION: {
				s32 mouseX = event.motion.x;
				s32 mouseY = event.motion.y;

				input->mouseInPixels.x = (double)mouseX;
				input->mouseInPixels.y = (double)(windowHeight - mouseY);

				V2 mouseInMeters = input->mouseInPixels * (1.0 / pixelsPerMeter);

				input->dMouseMeters = mouseInMeters - input->mouseInMeters;

				input->mouseInMeters = mouseInMeters;
				input->mouseInWorld = input->mouseInMeters + cameraP;

			} break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if (event.button.button == SDL_BUTTON_LEFT) {
					if (event.button.state == SDL_PRESSED) {
						input->leftMouse.pressed = input->leftMouse.justPressed = true;
					} else {
						input->leftMouse.pressed = input->leftMouse.justPressed = false;
					}
				}

				if (event.button.button == SDL_BUTTON_RIGHT) {
					if (event.button.state == SDL_PRESSED) {
						input->rightMouse.pressed = input->rightMouse.justPressed = true;
					} else {
						input->rightMouse.pressed = input->rightMouse.justPressed = false;
					}
				}

				break;
		}
	}
}

SDL_Window* createWindow(s32 windowWidth, s32 windowHeight) {
	//TODO: Proper error handling if any of these libraries does not load
	//TODO: Only initialize what is needed
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		fprintf(stderr, "Failed to initialize SDL. Error: %s", SDL_GetError());
		InvalidCodePath;
	}

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

	if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) {
		fprintf(stderr, "Failed to initialize SDL_image.");
		InvalidCodePath;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

	#if 1
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1); 
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
	#endif

	SDL_GL_SetSwapInterval(1); //Enables vysnc

	SDL_Window* window = SDL_CreateWindow("Hackformer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
										  windowWidth, windowHeight, 
										  SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_OPENGL);

	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	assert(glContext);

	SDL_GL_SetSwapInterval(1); //Enables vysnc

	glDisable(GL_DEPTH_TEST);

	GLenum glewStatus = glewInit();

	if (glewStatus != GLEW_OK) {
		fprintf(stderr, "Failed to initialize glew. Error: %s\n", glewGetErrorString(glewStatus));
		InvalidCodePath;
	}

	if (!window) {
		fprintf(stderr, "Failed to create window. Error: %s", SDL_GetError());
		InvalidCodePath;
	}

	if (TTF_Init()) {
		fprintf(stderr, "Failed to initialize SDL_ttf.");
		InvalidCodePath;
	}

	return window;
}