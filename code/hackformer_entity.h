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

enum EntityFlag {
	EntityFlag_facesLeft = 1 << 0,
	EntityFlag_noMovementByDefault = 1 << 1,
	EntityFlag_hackable = 1 << 2,
	EntityFlag_remove = 1 << 3,
	EntityFlag_removeWhenOutsideLevel = 1 << 4,
	EntityFlag_shooting = 1 << 5,
	EntityFlag_unchargingAfterShooting = 1 << 6,
	EntityFlag_grounded = 1 << 7,
	EntityFlag_laserOn = 1 << 8,
	EntityFlag_movedByGround = 1 << 9,
	EntityFlag_animIntro = 1 << 10,
	EntityFlag_animOutro = 1 << 11,
	EntityFlag_wasSolid = 1 << 12, //Used by pickup fields
};

struct RefNode {
	s32 ref;
	RefNode* next;
};

struct Hitbox {
	V2 collisionSize;
	V2 collisionOffset;
	Hitbox* next;
};

struct Messages {
	s32 count;
	s32 selectedIndex;
	char text[10][100];
	TextureData textures[10];

	//NOTE: This is currently used for the free list
	//TODO: This could be used to support having more than 10 messages
	Messages* next;
};

struct Entity {
	s32 ref;
	EntityType type;
	u32 flags;

	V2 p;
	V2 dP;

	V2 renderSize;
	DrawOrder drawOrder;

	Hitbox* hitboxes;
	R2 clickBox;

	s32 numFields;
	ConsoleField* fields[8];

	RefNode* groundReferenceList;
	RefNode* ignorePenetrationList;

	float emissivity;
	double spotLightAngle;

	//Used by any entity that shoots
	//TODO: This could be stored in the shoot console field
	double shootTimer;
	s32 targetRef;

	//Used by projectiles
	s32 shooterRef;

	//Used by tiles and player death
	V2 startPos;

	//Used by tiles
	s32 tileXOffset;
	s32 tileYOffset;

	//Used by any entity that jumps
	s32 jumpCount;
	double timeSinceLastOnGround;

	//Used by text entity
	Messages* messages;

	double animTime;
	AnimNode currentAnim;
	AnimNode nextAnim;

	Texture defaultTex;
	CharacterAnim characterAnim;
};

struct EntityHackSave {
	V2 p;
	s32 tileXOffset;
	s32 tileYOffset;
	s32 messagesSelectedIndex;
	double timeSinceLastOnGround;
	s32 numFields;
};

struct EntityReference {
	Entity* entity;
	EntityReference* next;
};

struct TileMoveNode;
struct TileMove {
	Entity* tile;
	TileMoveNode* children;
	int numParents;
};

struct TileMoveNode {
	TileMove* move;
	TileMoveNode* next;
};

struct GetCollisionTimeResult {
	double collisionTime;
	Entity* hitEntity;
	bool horizontalCollision;

	Entity* solidEntity;
	double solidCollisionTime;
	bool solidHorizontalCollision;
};

void setFlags(Entity* entity, u32 flags) {
	entity->flags |= flags;
} 

void clearFlags(Entity* entity, u32 flags) {
	entity->flags &= ~flags;
} 

void toggleFlags(Entity* entity, u32 toggle) {
	entity->flags ^= toggle;
}

bool32 isSet(Entity* entity, u32 flags) {
	bool32 result = (entity->flags & flags) != 0;
	return result;
}

bool isTileType(Entity* entity) {
	bool result = entity->type == EntityType_tile || entity->type == EntityType_heavyTile;
	return result;
}

Entity* getEntityByRef(GameState*, s32 ref);
ConsoleField* getMovementField(Entity* entity);
ConsoleField* getField(Entity* entity, ConsoleFieldType type);
bool isMouseInside(Entity* entity, Input* input);
void removeTargetRef(int, GameState*);
void addTargetRef(int, GameState*);
void addField(Entity*, ConsoleField*);
void ignoreAllPenetratingEntities(Entity*, GameState*);
