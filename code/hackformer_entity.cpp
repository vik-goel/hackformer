Hitbox* createHitbox(GameState* gameState) {
	Hitbox* hitbox = NULL;

	if (gameState->hitboxFreeList) {
		hitbox = gameState->hitboxFreeList;
		gameState->hitboxFreeList = gameState->hitboxFreeList->next;
	} else {
		hitbox = pushStruct(&gameState->levelStorage, Hitbox);
	}

	*hitbox = {};

	return hitbox;
}

void giveEntityRectangularCollisionBounds(Entity* entity, GameState* gameState,
										  double xOffset, double yOffset, double width, double height) {
	Hitbox* hitbox = createHitbox(gameState);

	hitbox->collisionSize = v2(width, height);
	hitbox->collisionOffset = v2(xOffset, yOffset);

	hitbox->next = entity->hitboxes;
	entity->hitboxes = hitbox;
}

RefNode* refNode(GameState* gameState, s32 ref, RefNode* next = NULL) {
	RefNode* result = NULL;

	if (gameState->refNodeFreeList) {
		result = gameState->refNodeFreeList;
		gameState->refNodeFreeList = gameState->refNodeFreeList->next;
	} else {
		result = (RefNode*)pushStruct(&gameState->levelStorage, RefNode);
	}

	assert(result);

	result->next = next;
	result->ref = ref;

	return result;
}

void freeRefNode(RefNode* node, GameState* gameState) {
	if (node->next) {
		freeRefNode(node->next, gameState);
	}

	node->ref = 0;
	node->next = gameState->refNodeFreeList;
	gameState->refNodeFreeList = node;
}

Messages* createMessages(GameState* gameState, char text[10][100], s32 count, s32 selectedIndex) {
	Messages* result = NULL;

	if (gameState->messagesFreeList) {
		result = gameState->messagesFreeList;
		gameState->messagesFreeList = gameState->messagesFreeList->next;
	} else {
		result = pushStruct(&gameState->levelStorage, Messages);
	}

	assert(result);

	result->count = count;
	result->next = NULL;

	assert(count < arrayCount(result->textures) - 1);
	assert(arrayCount(result->textures) == arrayCount(result->text));

	for(s32 textIndex = 0; textIndex <= count; textIndex++) {
		result->textures[textIndex].texId = 0;
		strcpy(result->text[textIndex], text[textIndex]);
	}

	return result;
}

void freeMessages(Messages* messages, GameState* gameState) {
	assert(!messages->next);

	messages->next = gameState->messagesFreeList;
	gameState->messagesFreeList = messages;
}

void removeTargetRef(s32 ref, GameState* gameState) {
	RefNode* node = gameState->targetRefs;
	RefNode* prevNode = NULL;

	bool removed = false;

	while(node) {
		if(node->ref == ref) {
			if(prevNode) prevNode->next = node->next;
			else gameState->targetRefs = node->next;

			node->next = NULL;
			freeRefNode(node, gameState);

			removed = true;
			break;
		}

		prevNode = node;
		node = node->next;
	}

	assert(removed);
}

void addTargetRef(s32 ref, GameState* gameState) {
	RefNode* node = refNode(gameState, ref, gameState->targetRefs);
	gameState->targetRefs = node;
}

EntityReference* getEntityReferenceBucket(GameState* gameState, s32 ref) {
	s32 bucketIndex = (ref % (arrayCount(gameState->entityRefs_) - 1)) + 1;
	EntityReference* result = gameState->entityRefs_ + bucketIndex;
	return result;
}

EntityReference* getPreciseEntityReferenceBucket(GameState* gameState, s32 ref) {
	EntityReference* bucket = getEntityReferenceBucket(gameState, ref);

	EntityReference* result = NULL;
	
	while(bucket) {
		if (bucket->entity && bucket->entity->ref == ref) {
			result = bucket;
			break;
		}

		bucket = bucket->next;
	}

	return result;
		}

Entity* getEntityByRef(GameState* gameState, s32 ref) {
	EntityReference* bucket = getPreciseEntityReferenceBucket(gameState, ref);

	Entity* result = NULL;
	if (bucket) result = bucket->entity;

	return result;
}

Entity* getUnremovedEntityByRef(GameState* gameState, s32 ref) {
	Entity* result = getEntityByRef(gameState, ref);
	if (result && isSet(result, EntityFlag_remove)) result = NULL;
	return result;
}

void storeEntityReferenceInFreeList(EntityReference* reference, GameState* gameState) {
	if (reference->next) {
		storeEntityReferenceInFreeList(reference->next, gameState);
		reference->next = NULL;
	}

	reference->entity = NULL;
	reference->next = gameState->entityRefFreeList;
	gameState->entityRefFreeList = reference;
}

void freeEntityReference(EntityReference* reference, GameState* gameState) {
	EntityReference* nextReference = reference->next;	

	if (reference->next) {
		storeEntityReferenceInFreeList(reference->next, gameState);
	}

	reference->entity = NULL;
	reference->next = NULL;
}

EntityChunk* getSpatialChunk(V2 p, GameState* gameState) {
	s32 x = (s32)(p.x / gameState->chunkSize.x);
	s32 y = (s32)(p.y / gameState->chunkSize.y);

	EntityChunk* result = NULL;

	if (x >= 0 && 
		y >= 0 && 
		x < gameState->chunksWidth &&
		y < gameState->chunksHeight) {
		
		result = gameState->chunks + y * gameState->chunksWidth + x;
	}

	return result;
}

void addToSpatialPartition(Entity* entity, GameState* gameState) {
	EntityChunk* chunk = getSpatialChunk(entity->p, gameState);

	while(chunk) {
		if (chunk->numRefs < arrayCount(chunk->entityRefs)) {
			chunk->entityRefs[chunk->numRefs++] = entity->ref;
			break;
		} 

		if (!chunk->next) {
			chunk->next = pushStruct(&gameState->levelStorage, EntityChunk);
			*chunk->next = {};
		}

		chunk = chunk->next;
	}
}

bool removeFromSpatialPartition(Entity* entity, GameState* gameState) {
	EntityChunk* chunk = getSpatialChunk(entity->p, gameState);

	bool removed = false;

	while(chunk) {
		for (s32 refIndex = 0; refIndex < chunk->numRefs; refIndex++) {
			if (chunk->entityRefs[refIndex] == entity->ref) {
				chunk->numRefs--;
				chunk->entityRefs[refIndex] = chunk->entityRefs[chunk->numRefs];
				removed = true;
				break;
			}
		}

		if (removed) break;

		chunk = chunk->next;
	}

	return removed;
}

void attemptToRemovePenetrationReferences(Entity*, GameState*);

void setEntityP(Entity* entity, V2 newP, GameState* gameState) {
	removeFromSpatialPartition(entity, gameState);
	entity->p = newP;
	addToSpatialPartition(entity, gameState);

	attemptToRemovePenetrationReferences(entity, gameState);
}

bool affectedByGravity(Entity* entity, ConsoleField* movementField) {
	bool result = true;

	bool32 noMovementByDefault = isSet(entity, EntityFlag_noMovementByDefault);

	if(movementField) {
		switch(movementField->type) {
			case ConsoleField_keyboardControlled: {
				result = true;
			} break;
			case ConsoleField_movesBackAndForth: {
				return !noMovementByDefault;
			} break;
			case ConsoleField_bobsVertically:
			case ConsoleField_followsWaypoints:
			case ConsoleField_seeksTarget: {
				result = false;
			} break;

			InvalidDefaultCase;
		}
	} else {
		result = !noMovementByDefault;
	}

	return result;
}

bool refNodeListContainsRef(RefNode* list, s32 ref) {
	RefNode* node = list;

	while(node) {
		if (node->ref == ref) return true;
		node = node->next;
	}

	return false;
}

void addGroundReference(Entity* top, Entity* ground, GameState* gameState, bool isLaserBaseToBeamReference = false) {
	ConsoleField* topMovementField = getMovementField(top);

	if(top->type == EntityType_tile && !topMovementField) return;
	if(!affectedByGravity(top, topMovementField) && !isLaserBaseToBeamReference) return;

	top->jumpCount = 0;
	setFlags(top, EntityFlag_grounded);
	top->timeSinceLastOnGround = 0;

	if(refNodeListContainsRef(ground->groundReferenceList, top->ref)) return;
	if(refNodeListContainsRef(top->groundReferenceList, ground->ref)) return;

	RefNode* append = refNode(gameState, top->ref);

	RefNode* last = ground->groundReferenceList;
	RefNode* previous = NULL;

	while (last) {
		previous = last;
		last = last->next;
	}

	if (previous) {
		previous->next = append;
	} else {
		ground->groundReferenceList = append;
	}
}

bool createsPickupFieldsOnDeath(Entity* entity) {
	bool result = true;

	//NOTE: This is just a hack to make the player death system work
	//		Though the player should never make pickup fields because the level
	//		should always end when the player is removed anyways
	if(entity->type == EntityType_player) result = false;

	return result;
}

Entity* addPickupField(GameState*, Entity*, ConsoleField*);

void freeEntity(Entity* entity, GameState* gameState, bool endOfLevel) {
	Messages* messages = entity->messages;
	if(messages) {
		for (s32 messageIndex = 0; messageIndex < messages->count; messageIndex++) {
			freeTexture(messages->textures[messageIndex]);
		}

		freeMessages(messages, gameState);
	}

	//NOTE: At the end of the level no pickup fields should be created and no memory needs to be free
	//		because the level storage will just be reset. 
	if(!endOfLevel) {
		s32 startFieldIndex = entity->type == EntityType_pickupField ? 1 : 0;
		for(s32 fieldIndex = startFieldIndex; fieldIndex < entity->numFields; fieldIndex++) {
			ConsoleField* field = entity->fields[fieldIndex];

			if(field->type == ConsoleField_givesEnergy) {
				gameState->fieldSpec.hackEnergy += field->children[0]->selectedIndex;
			}
		}

		if(createsPickupFieldsOnDeath(entity)) {
			for (s32 fieldIndex = 0; fieldIndex < entity->numFields; fieldIndex++) {
				if(entity->fields[fieldIndex]) {
					if(!hasValues(entity->fields[fieldIndex]) && entity->fields[fieldIndex]->type != ConsoleField_givesEnergy) {
						Entity* pickupField = addPickupField(gameState, entity, entity->fields[fieldIndex]);

						pickupField->dP.x = (fieldIndex / 2 + 1) * 30;
						pickupField->dP.y = (fieldIndex / 2 + 1) * 4;

						if(fieldIndex % 2 == 0) pickupField->dP.x *= -1;
					} else {
						freeConsoleField(entity->fields[fieldIndex], gameState);
					}

					entity->fields[fieldIndex] = NULL;
				}
			}
		} else {
			for (s32 fieldIndex = 0; fieldIndex < entity->numFields; fieldIndex++) {
				freeConsoleField(entity->fields[fieldIndex], gameState);
				entity->fields[fieldIndex] = NULL;
			}
		}

		if(entity->groundReferenceList) {
			freeRefNode(entity->groundReferenceList, gameState);
			entity->groundReferenceList = NULL;
		}

		if(entity->ignorePenetrationList) {
			freeRefNode(entity->ignorePenetrationList, gameState);
			entity->ignorePenetrationList = NULL;
		}

		Hitbox* box = entity->hitboxes;
		Hitbox* secondBox = NULL;

		while(box) {
			secondBox = box;
			box = box->next;
		}

		if(secondBox) {
			assert(!secondBox->next);
			secondBox->next = gameState->hitboxFreeList;
			gameState->hitboxFreeList = secondBox->next;
		}
	}

	entity->numFields = 0;
}

void removeEntities(GameState* gameState) {

	//NOTE: No removes are allowed while in console mode because this would break updating
	//		entities with their old saved values. 
	if(getEntityByRef(gameState, gameState->consoleEntityRef)) return;

	//NOTE: Entities are removed here if their remove flag is set
	//NOTE: There is a memory leak here if the entity allocated anything
	//		Ensure this is cleaned up, the console is a big place where leaks can happen
	for (s32 entityIndex = 0; entityIndex < gameState->numEntities; entityIndex++) {
		Entity* entity = gameState->entities + entityIndex;

		if (isSet(entity, EntityFlag_remove)) {
			freeEntity(entity, gameState, false);

			EntityReference* bucket = getEntityReferenceBucket(gameState, entity->ref);
			EntityReference* previousBucket = bucket;

			while (bucket) {
				if (bucket->entity->ref == entity->ref) {
					break;
				}

				previousBucket = bucket;
				bucket = bucket->next;
			}

			if (bucket != NULL) previousBucket->next = bucket->next;
			else previousBucket->next = NULL;


			//NOTE: EntityReference's which are inside the entityRefs_ array should not be
			//		added to the free list. This checks the address of an entity reference before 
			//		adding it to the free list to ensure that it is not part of the table.
			size_t bucketAddress = (size_t)bucket;
			size_t entityRefsStartAddress = (size_t)&gameState->entityRefs_;
			size_t entityRefsEndAddress = entityRefsStartAddress + sizeof(gameState->entityRefs_);

			if (bucketAddress < entityRefsStartAddress || bucketAddress >= entityRefsEndAddress) {
				//Only adds if outside of the address space of the table
				bucket->next = gameState->entityRefFreeList;
				gameState->entityRefFreeList = bucket;
			}
			
			bucket->entity = NULL;

			gameState->numEntities--;

			if(entityIndex != gameState->numEntities) {
				Entity* dst = gameState->entities + entityIndex;
				Entity* src = gameState->entities + gameState->numEntities;
				*dst = *src;
				getPreciseEntityReferenceBucket(gameState, dst->ref)->entity -= (gameState->numEntities - entityIndex);
			}

			entityIndex--;

			gameState->entities[gameState->numEntities] = {};
		}
	}
}

void initEntityReference(Entity* entity, GameState* gameState) {
	EntityReference* bucket = getEntityReferenceBucket(gameState, entity->ref);
	while(bucket->next) {
		bucket = bucket->next;
	}

	if (bucket->entity) {
		if (gameState->entityRefFreeList) {
			bucket->next = gameState->entityRefFreeList;
			gameState->entityRefFreeList = gameState->entityRefFreeList->next;
		} else {
			bucket->next = pushStruct(&gameState->levelStorage, EntityReference);
			*bucket->next = {};
		}

		bucket = bucket->next;
		bucket->next = NULL;
	}

	bucket->entity = entity;
}

Entity* addEntity(GameState* gameState, s32 ref, s32 entityIndex) {
	assert(entityIndex >= 0);	
	assert(entityIndex < arrayCount(gameState->entities));

	Entity* result = gameState->entities + entityIndex;
	*result = {};

	result->ref = ref;
	initEntityReference(result, gameState);

	return result;
}

Entity* addEntity(GameState* gameState, EntityType type, DrawOrder drawOrder, V2 p, V2 renderSize) {
	assert(gameState->numEntities < arrayCount(gameState->entities));

	Entity* result = gameState->entities + gameState->numEntities;

	*result = {};
	result->ref = gameState->refCount;
	result->type = type;
	result->drawOrder = drawOrder;
	result->p = p;
	result->renderSize = renderSize;

	initEntityReference(result, gameState);

	gameState->numEntities++;
	gameState->refCount++;

	addToSpatialPartition(result, gameState);

	return result;
}

void removeField(ConsoleField** fields, s32 numFields, s32 fieldIndex) {
	assert(fieldIndex >= 0 && fieldIndex < numFields);

	clearFlags(fields[fieldIndex], ConsoleFlag_remove);

	for (s32 moveIndex = fieldIndex; moveIndex < numFields - 1; moveIndex++) {
		ConsoleField** dst = fields + moveIndex;
		ConsoleField** src = fields + moveIndex + 1;

		*dst = *src;
	}

	fields[numFields - 1] = NULL;
}

void removeFieldsIfSet(ConsoleField** fields, s32* numFields) {
	for (s32 fieldIndex = 0; fieldIndex < *numFields; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];

		if (field->children) {
			removeFieldsIfSet(field->children, &field->numChildren);
		}

		if (isSet(field, ConsoleFlag_remove)) {
			removeField(fields, *numFields, fieldIndex);
			*numFields = *numFields - 1;
		}
	}
}

void addField(Entity* entity, ConsoleField* field) {
	entity->fields[entity->numFields] = field;
	entity->numFields++;
	assert(arrayCount(entity->fields) > entity->numFields);
}

void addChildToConsoleField(ConsoleField* parent, ConsoleField* child) {
	parent->children[parent->numChildren] = child;
	parent->numChildren++;
	assert(parent->numChildren <= arrayCount(parent->children));
}

//TODO: Tweak speed and jump height values
void addKeyboardField(Entity* entity, GameState* gameState) {
	ConsoleField* result = createConsoleField(gameState, "keyboard_controlled", ConsoleField_keyboardControlled, 5);

	double keyboardSpeedFieldValues[] = {20, 40, 60, 80, 100}; 
	double keyboardJumpHeightFieldValues[] = {1, 3, 5, 7, 9}; 

	addChildToConsoleField(result, createPrimitiveField(double, gameState, "speed", keyboardSpeedFieldValues, 
												   arrayCount(keyboardSpeedFieldValues), 2, 1));
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "jump_height", keyboardJumpHeightFieldValues, 
													    arrayCount(keyboardJumpHeightFieldValues), 2, 1));
	addChildToConsoleField(result, createBoolField(gameState, "double_jump", false, 3));

	addField(entity, result);
}

void addBobsVerticallyField(Entity* entity, GameState* gameState) {
	ConsoleField* result = createConsoleField(gameState, "bobs_vertically", ConsoleField_bobsVertically, 4);

	double speedFieldValues[] = {0, 1.5, 3, 4.5, 6}; 
	double maxBobHeight[] = {0, 0.5, 1, 1.5, 2}; 

	addChildToConsoleField(result, createPrimitiveField(double, gameState, "speed", speedFieldValues, 
												   arrayCount(speedFieldValues), 2, 1));
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "max_bob_height", maxBobHeight, 
													    arrayCount(maxBobHeight), 2, 1));

	result->bobbingUp = true;
	result->initialBob = true;

	addField(entity, result);
}

void addPatrolField(Entity* entity, GameState* gameState) {
	ConsoleField* result = createConsoleField(gameState, "patrols", ConsoleField_movesBackAndForth, 3);

	double patrolSpeedFieldValues[] = {0, 10, 20, 30, 40}; 
	
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "speed", patrolSpeedFieldValues, 
	 											   arrayCount(patrolSpeedFieldValues), 2, 1));

	addChildToConsoleField(result, createEnumField(Alertness, gameState, "alertness", Alertness_patrolling, 5));

	addField(entity, result);
}

void addSeekTargetField(Entity* entity, GameState* gameState) {
	ConsoleField* result = createConsoleField(gameState, "seeks_target", ConsoleField_seeksTarget, 3);

	double seekTargetSpeedFieldValues[] = {5, 10, 15, 20, 25}; 
	double seekTargetRadiusFieldValues[] = {4, 8, 12, 16, 20}; 
	
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "speed", seekTargetSpeedFieldValues, 
	 												    arrayCount(seekTargetSpeedFieldValues), 2, 1));
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "sight_radius", seekTargetRadiusFieldValues, 
													    arrayCount(seekTargetRadiusFieldValues), 2, 2));
	addChildToConsoleField(result, createEnumField(Alertness, gameState, "alertness", Alertness_patrolling, 5));

	addField(entity, result);
}

Waypoint* createWaypoint(GameState* gameState, V2 p, Waypoint* next = NULL) {
	Waypoint* result = pushStruct(&gameState->levelStorage, Waypoint);
	*result = {};
	result->p = p;
	result->next = next;
	return result;
}

void addFollowsWaypointsField(Entity* entity, GameState* gameState) {
	ConsoleField* result = createConsoleField(gameState, "follows_waypoints", ConsoleField_followsWaypoints, 3);

	double followsWaypointsSpeedFieldValues[] = {1, 2, 3, 4, 5}; 
	double waypointDelays[] = {0, 0.5, 1, 1.5, 2};
	
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "speed", followsWaypointsSpeedFieldValues, 
	 												    arrayCount(followsWaypointsSpeedFieldValues), 2, 1));
	addChildToConsoleField(result, createEnumField(Alertness, gameState, "alertness", Alertness_patrolling, 5));
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "waypoint_delay", waypointDelays, 
	 												    arrayCount(waypointDelays), 2, 1));

	s32 numWaypoints = 4;
	Waypoint* waypoints = pushArray(&gameState->levelStorage, Waypoint, numWaypoints);

	for(s32 waypointIndex = 0; waypointIndex < numWaypoints; waypointIndex++) {
		Waypoint* w = waypoints + waypointIndex;
		Waypoint* next = waypoints + (waypointIndex + 1) % numWaypoints;
		w->next = next;
	}

	waypoints[0].p = v2(2, 4);
	waypoints[1].p = v2(5, 7);
	waypoints[2].p = v2(8, 6);
	waypoints[3].p = v2(11, 3);

	result->curWaypoint = waypoints;
	result->waypointDelay = 0;

	addField(entity, result);
}

void addGivesBlueEnergyField(Entity* entity, GameState* gameState) {
	ConsoleField* result = createConsoleField(gameState, "gives_energy", ConsoleField_givesEnergy, 1);

	addChildToConsoleField(result, createUnlimitedIntField(gameState, "amount", 1, 1));

	addField(entity, result);
}

void addShootField(Entity* entity, GameState* gameState) {
	ConsoleField* result = createConsoleField(gameState, "shoots", ConsoleField_shootsAtTarget, 4);

	double bulletSpeedFieldValues[] = {1, 2, 3, 4, 5}; 
	double sightRadii[] = {2, 4, 6, 8, 10};

	addChildToConsoleField(result, createPrimitiveField(double, gameState, "bullet_speed", bulletSpeedFieldValues, 
													    arrayCount(bulletSpeedFieldValues), 2, 1));

	addField(entity, result);
}

void addSpotlightField(Entity* entity, GameState* gameState) {
	ConsoleField* result = createConsoleField(gameState, "spotlight", ConsoleField_spotlight, 15);

	double sightRadii[] = {2, 4, 6, 8, 10};
	double fovs[] = {15, 30, 45, 60, 75};
	
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "sight_radius", sightRadii, 
													    arrayCount(sightRadii), 2, 2));
	addChildToConsoleField(result, createPrimitiveField(double, gameState, "field_of_view", fovs, 
													    arrayCount(fovs), 2, 1));
	addField(entity, result);
}

void addIsTargetField(Entity* entity, GameState* gameState) {
	ConsoleField* field = createConsoleField(gameState, "is_target", ConsoleField_isShootTarget, 10);

	addField(entity, field);
	addTargetRef(entity->ref, gameState);
}


Entity* addPlayerDeath(GameState* gameState) {
	assert(!getEntityByRef(gameState, gameState->playerDeathRef));

	Entity* player = getEntityByRef(gameState, gameState->playerRef);
	assert(player);

	V2 p = gameState->playerDeathStartP;
	p += (player->renderSize - gameState->playerDeathSize) * 0.5;

	Entity* result = addEntity(gameState, EntityType_playerDeath, DrawOrder_playerDeath, 
								p, player->renderSize);

	gameState->playerDeathRef = result->ref;

	setFlags(result, EntityFlag_noMovementByDefault);

	result->characterAnim = gameState->playerDeathAnim;

	return result;
}


Entity* addPlayer(GameState* gameState, V2 p) {
	Entity* result = addEntity(gameState, EntityType_player, DrawOrder_player, p, v2(1, 1) * 1.75);

	assert(!getUnremovedEntityByRef(gameState, gameState->playerRef));
	gameState->playerRef = result->ref;

	giveEntityRectangularCollisionBounds(result, gameState, result->renderSize.x * -0.01, 0,
										 result->renderSize.x * 0.39, result->renderSize.y);

	result->clickBox = rectCenterDiameter(v2(0, 0), v2(result->renderSize.x * 0.4, result->renderSize.y));

	setFlags(result, EntityFlag_hackable);

	addKeyboardField(result, gameState);
	
	addField(result, createConsoleField(gameState, "camera_followed", ConsoleField_cameraFollows, 2));
	addIsTargetField(result, gameState);

	setEntityP(result, result->p + result->renderSize * 0.5, gameState);

	result->characterAnim = gameState->playerAnim;

	return result;
}

TextureData* getStandTex(CharacterAnim characterAnim, GameState* gameState) {
	assert(gameState);
	assert(characterAnim.dataIndex);
	CharacterAnimData* characterAnimData = getCharacterAnimData(characterAnim, gameState);
	assert(characterAnimData);
	AnimNodeData* standData = getAnimNodeData(characterAnimData->standAnim, gameState);
	assert(standData);
	Animation* standAnim = &standData->main;
	assert(standAnim->frames > 0);
	Texture* standTex = standAnim->frames + 0;
	assert(standTex && standTex->dataIndex > 0);
	TextureData* standTexData = getTextureData(standTex, gameState->renderGroup);
	assert(standTexData);
	return standTexData;
}

Entity* addVirus(GameState* gameState, V2 p) {
	V2 size = getDrawSize(getStandTex(gameState->virus1Anim, gameState), 1.6);
	Entity* result = addEntity(gameState, EntityType_virus, DrawOrder_virus, p, size);

	double heightOffs = 0.4;
	giveEntityRectangularCollisionBounds(result, gameState, 0, 0,
										 result->renderSize.x, result->renderSize.y + heightOffs);

	result->clickBox = rectCenterDiameter(v2(0, 0), result->renderSize);

	setFlags(result, EntityFlag_facesLeft|
					 EntityFlag_hackable);

	addPatrolField(result, gameState);
	addShootField(result, gameState);
	addSpotlightField(result, gameState);

	setEntityP(result, result->p + result->renderSize * 0.5, gameState);

	result->characterAnim = gameState->virus1Anim;

	return result;
}

Entity* addEndPortal(GameState* gameState, V2 p) {
	Entity* result = addEntity(gameState, EntityType_endPortal, DrawOrder_endPortal, p, v2(1, 2));

	giveEntityRectangularCollisionBounds(result, gameState, 0, 0,
										 result->renderSize.x * 0.4f, result->renderSize.y * 0.95f);

	setEntityP(result, result->p + result->renderSize * 0.5, gameState);

	result->defaultTex = gameState->endPortal;
	result->emissivity = 0.75f;

	return result;
}

Entity* addPickupField(GameState* gameState, Entity* parent, ConsoleField* field) {
	Entity* result = addEntity(gameState, EntityType_pickupField, DrawOrder_pickupField, parent->p, gameState->fieldSpec.fieldSize);

	giveEntityRectangularCollisionBounds(result, gameState, 0, 0, result->renderSize.x, result->renderSize.y);
	result->clickBox = rectCenterDiameter(v2(0, 0), result->renderSize);

	addField(result, field);
	addField(result, createBoolField(gameState, "solid", false, 1));
	addField(result, createBoolField(gameState, "can_pickup", true, 1));

	result->emissivity = 1.0f;
	setFlags(result, EntityFlag_removeWhenOutsideLevel|
					 EntityFlag_hackable);

	ignoreAllPenetratingEntities(result, gameState);

	return result;
}

Entity* addHackEnergy(GameState* gameState, V2 p) {
	Entity* result = addEntity(gameState, EntityType_hackEnergy, DrawOrder_hackEnergy, p, v2(1, 1) * 0.6);

	giveEntityRectangularCollisionBounds(result, gameState, 0, 0, 
										 result->renderSize.x, result->renderSize.y - 0.18);

	addGivesBlueEnergyField(result, gameState);
	addBobsVerticallyField(result, gameState);

	setFlags(result, EntityFlag_hackable);

	setEntityP(result, result->p + result->renderSize * 0.5, gameState);
	result->clickBox = rectCenterDiameter(v2(0, 0), result->renderSize);

	return result;
}

Entity* addBackground(GameState* gameState) {
	Entity* result = addEntity(gameState, EntityType_background, DrawOrder_background, 
								  v2(0, 0), v2(gameState->mapSize.x, 
								(double)gameState->windowHeight / (double)gameState->pixelsPerMeter));

	setEntityP(result, result->renderSize * 0.5, gameState);
	setFlags(result, EntityFlag_noMovementByDefault);

	return result;
}

void initTile(Entity* tile, GameState* gameState, Texture texture) {
	tile->renderSize = gameState->tileSize;

	double collisionHeight = tile->renderSize.y * 0.9;

	giveEntityRectangularCollisionBounds(tile, gameState, 0, (collisionHeight - tile->renderSize.y) / 2, 
										 tile->renderSize.x, collisionHeight);

	tile->clickBox = rectCenterDiameter(v2(0, 0), tile->renderSize);

	setFlags(tile, EntityFlag_hackable);

	addField(tile, createUnlimitedIntField(gameState, "x_offset", 0, 1));
	addField(tile, createUnlimitedIntField(gameState, "y_offset", 0, 1));
}

Entity* addTile(GameState* gameState, V2 p, Texture texture) {
	Entity* result = addEntity(gameState, EntityType_tile, DrawOrder_tile, 
								p, v2(0, 0));

	initTile(result, gameState, texture);

	setFlags(result, EntityFlag_noMovementByDefault|
					 EntityFlag_removeWhenOutsideLevel);

	result->defaultTex = texture;

	return result;
}

Entity* addHeavyTile(GameState* gameState, V2 p) {
	Entity* result = addEntity(gameState, EntityType_heavyTile, DrawOrder_heavyTile, 
								p, v2(0, 0));
	initTile(result, gameState, gameState->tileAtlas[Tile_heavy]);
	result->defaultTex = gameState->tileAtlas[Tile_heavy];

	return result;
}

void setSelectedText(Entity* text, s32 selectedIndex, GameState* gameState) {
	assert(text->type == EntityType_text);

	Messages* messages = text->messages;
	assert(messages);

	messages->selectedIndex = selectedIndex;

	TextureData* texture = messages->textures + selectedIndex;

	if(!texture->texId) {
		*texture = createText(gameState->renderGroup, gameState->textFont, messages->text[selectedIndex]);
	}

	text->renderSize = texture->size;
	text->clickBox = rectCenterDiameter(v2(0, 0), text->renderSize);
}

Entity* addText(GameState* gameState, V2 p, char values[10][100], s32 numValues, s32 selectedIndex) {
	Entity* result = addEntity(gameState, EntityType_text, DrawOrder_text, p, v2(0, 0));

	result->messages = createMessages(gameState, values, numValues, selectedIndex);
	setSelectedText(result, selectedIndex, gameState);

	giveEntityRectangularCollisionBounds(result, gameState, 0, 0, 
										 result->renderSize.x, result->renderSize.y);

	setFlags(result, EntityFlag_noMovementByDefault|
					 EntityFlag_hackable);

	s32 selectedIndexValues[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	addField(result, createPrimitiveField(s32, gameState, "selected_index", selectedIndexValues, numValues + 1, selectedIndex, 1));

	return result;
}

Entity* addLaserBolt(GameState* gameState, V2 p, V2 target, s32 shooterRef, double speed) {
	Entity* result = addEntity(gameState, EntityType_laserBolt, DrawOrder_laserBolt, p, v2(0.24f, 0.24f));

	giveEntityRectangularCollisionBounds(result, gameState, 0, 0, 
										 result->renderSize.x, result->renderSize.y);

	result->clickBox = rectCenterDiameter(v2(0, 0), result->renderSize);

	result->dP = normalize(target - p) * speed;
	result->shooterRef = shooterRef;

	setFlags(result, EntityFlag_removeWhenOutsideLevel|
					 EntityFlag_hackable);

	addField(result, createBoolField(gameState, "hurts_entities", true, 2));
	addField(result, createConsoleField(gameState, "kills_enemies", ConsoleField_killsOnHit, 10));

	result->defaultTex = gameState->laserBolt;

	result->emissivity = 1;
					 
	return result;
}

Entity* addLaserBase_(GameState* gameState, V2 baseP, double height) {
	Entity* result = addEntity(gameState, EntityType_laserBase, DrawOrder_laserBase, baseP, v2(0.9, 0.65));

	addField(result, createBoolField(gameState, "enabled", true, 2));
	addPatrolField(result, gameState);

	giveEntityRectangularCollisionBounds(result, gameState, 0, 0, result->renderSize.x, result->renderSize.y);
	giveEntityRectangularCollisionBounds(result, gameState, 0, height, result->renderSize.x, result->renderSize.y);

	result->clickBox = rectCenterDiameter(v2(0, height / 2), v2(result->renderSize.x, height + result->renderSize.y));
	result->clickBox = addRadiusTo(result->clickBox, v2(0.1, 0.1));

	setFlags(result, EntityFlag_noMovementByDefault|
					 EntityFlag_laserOn|
					 EntityFlag_hackable);

	result->emissivity = 0.1f;

	return result;
}

Entity* addLaserBeam_(GameState* gameState, V2 p, V2 renderSize) {
	renderSize.x *= 0.8;

	Entity* result = addEntity(gameState, EntityType_laserBeam, DrawOrder_laserBeam, p, renderSize);
	result->defaultTex = gameState->laserBeam;

	giveEntityRectangularCollisionBounds(result, gameState, 0, 0, result->renderSize.x, result->renderSize.y);

	setFlags(result, EntityFlag_noMovementByDefault|
					 EntityFlag_laserOn);

	result->emissivity = 0.25f;

	return result;
}

Entity* addLaserController(GameState* gameState, V2 baseP, double height) {
	//NOTE: The order which all the parts are added: base first then beam
	//		is necessary. This is so that all of the other pieces of the laser can be easily found if you just
	//		have one to start. This is mainly used for collision detection (eg. avoiding collisions between the base
	//		and the other laser pieces)

	Entity* base = addLaserBase_(gameState, baseP, height);
	V2 topP = baseP + v2(0, height);
	// Entity* top = addLaserPiece_(gameState, topP, false);

	V2 p = (baseP + topP) * 0.5;

	V2 laserSize = v2(base->renderSize.x, height - base->renderSize.y / 2);
	V2 laserP = base->p + v2(0, height / 2);

	Entity* beam = addLaserBeam_(gameState, laserP, laserSize);

	addGroundReference(beam, base, gameState, true);

	return base;
}

Entity* addFlyingVirus(GameState* gameState, V2 p) {
	V2 size = getDrawSize(getStandTex(gameState->flyingVirusAnim, gameState), 0.75);
	Entity* result = addEntity(gameState, EntityType_flyingVirus, DrawOrder_flyingVirus, p, size);

	result->characterAnim = gameState->flyingVirusAnim;

	setFlags(result, EntityFlag_hackable|
					 EntityFlag_noMovementByDefault);

	giveEntityRectangularCollisionBounds(result, gameState, 0, 0, 
										 result->renderSize.x, result->renderSize.y);

	result->clickBox = rectCenterDiameter(v2(0, 0), result->renderSize);

	addFollowsWaypointsField(result, gameState);
	addShootField(result, gameState);
	addSpotlightField(result, gameState);

	setEntityP(result, result->p + result->renderSize  * 0.5, gameState);
	result->spotLightAngle = 210;
	result->emissivity = 0.4f;

	return result;
}

ConsoleField* getMovementField(Entity* entity) {
	ConsoleField* result = NULL;

	if(!isSet(entity, EntityFlag_remove)) {
		s32 firstFieldIndex = entity->type == EntityType_pickupField ? 1 : 0;
		for (s32 fieldIndex = firstFieldIndex; fieldIndex < entity->numFields; fieldIndex++) {
			ConsoleField* testField = entity->fields[fieldIndex];
			if (isConsoleFieldMovementType(testField)) {
				result = testField;
				break;
			}
		}
	}

	return result;
}

bool isLaserEntityType(Entity* entity) {
	bool result = entity->type == EntityType_laserBase ||
				  entity->type == EntityType_laserBeam;
	return result;
}

bool canPickupField(Entity* pickupField, Entity* collider, GameState* gameState) {
	assert(pickupField->type == EntityType_pickupField);

	bool result = false;

	if (!isSet(pickupField, EntityFlag_remove) && !gameState->swapField) {
		assert(pickupField->type == EntityType_pickupField);
		assert(pickupField->numFields >= 3);

		ConsoleField* field = pickupField->fields[2];
		bool32 canPickup = (field && field->selectedIndex);

		if(canPickup) {
			ConsoleField* field = getMovementField(collider);
			if(field && field->type == ConsoleField_keyboardControlled) result = true;
		}
	}

	return result;
}

bool32 isPickupFieldSolid(Entity* pickupField) {
	if(isSet(pickupField, EntityFlag_remove)) return false;

	assert(pickupField->type == EntityType_pickupField);
	assert(pickupField->numFields >= 2);

	ConsoleField* solidField = pickupField->fields[1];
	bool32 solid = (solidField && solidField->selectedIndex);

	return solid;
}

bool collidesWithRaw(Entity* a, Entity* b, GameState* gameState) {
	bool result = true;

	switch(a->type) {
		case EntityType_background: {
			result = false;
		} break;

		case EntityType_text: {
			result = getMovementField(a) != NULL;
		} break;

		case EntityType_player: {
			if (b->type == EntityType_virus ||
				b->type == EntityType_flyingVirus) result = false;
		} break;

		case EntityType_laserBolt: {
			if (b->ref == a->shooterRef) result = false;
			else {
				//NOTE: If a bullet is shot from a laser base/top, it should not collide with the laser beam
				//		This requires the base, top, and beam to all be added in a specific order to work
				Entity* shooter = getEntityByRef(gameState, a->shooterRef);
				if(shooter && shooter->type == EntityType_laserBase && shooter->ref + 1 == b->ref)
				    result = false;
			}
		} break;

		case EntityType_hackEnergy: {
			if (b->type == EntityType_virus ||
				b->type == EntityType_flyingVirus ||
				b->type == EntityType_laserBolt || 
				b->type == EntityType_pickupField) result = false;
		} break;

		case EntityType_endPortal: {
			if (b->type != EntityType_player && 
				b->type != EntityType_tile &&
				b->type != EntityType_heavyTile) result = false;
		} break;

		case EntityType_laserBeam: {
			//NOTE: These are the 2 other pieces of the laser (base, top)
			if (a->ref == b->ref + 1 || 
				!isSet(a, EntityFlag_laserOn)) result = false;
		} break;

		case EntityType_tile: {
			if (b->type == EntityType_tile && 
				getMovementField(a) == NULL && 
				getMovementField(b) == NULL) result = false;
		} break;

		case EntityType_pickupField: {
			result = false;

			if(isPickupFieldSolid(a) ||
			   canPickupField(a, b, gameState) || 
			  (b->type == EntityType_tile && getMovementField(b) == NULL)) result = true;

			if(b->type == EntityType_laserBeam || b->type == EntityType_laserBolt) result = false;
		} break;
	}

	return result;
}

bool isIgnoringPenetrationRaw(Entity* a, Entity* b) {
	bool result = refNodeListContainsRef(a->ignorePenetrationList, b->ref);
	return result;
}

bool isIgnoringPenetration(Entity* a, Entity* b) {
	bool result = isIgnoringPenetrationRaw(a, b) || isIgnoringPenetrationRaw(b, a);
	return result;
}

bool collidesWith(Entity* a, Entity* b, GameState* gameState) {
	bool result = !isSet(a, EntityFlag_remove) && !isSet(b, EntityFlag_remove);

	result &= !isIgnoringPenetration(a, b);
	result &= collidesWithRaw(a, b, gameState);
	result &= collidesWithRaw(b, a, gameState);

	return result;
}

bool isEntityAbove(Entity* top, Entity* bottom) {
	bool result = true;

	Hitbox* topHitboxes = top->hitboxes;

	while(topHitboxes && result) {
		Hitbox* bottomHitboxes = bottom->hitboxes;

		while(bottomHitboxes) {
			double bottomMax = bottom->p.y + bottomHitboxes->collisionOffset.y + bottomHitboxes->collisionSize.y / 2;
			double topMin = top->p.y + topHitboxes->collisionOffset.y - topHitboxes->collisionSize.y / 2;

			if (bottomMax > topMin) {
				result = false;
				break;
			}

			bottomHitboxes = bottomHitboxes->next;
		}

		topHitboxes = topHitboxes->next;
	}

	return result;
}

bool isSolidCollisionRaw(Entity* a, Entity* b, GameState* gameState) {
	bool result = true;

	switch(a->type) {
		case EntityType_laserBolt: 
		case EntityType_laserBeam: {
			result = false;
		} break;

		case EntityType_player: {
			if (b->type == EntityType_hackEnergy || 
				b->type == EntityType_endPortal) result = false;
		} break;

		case EntityType_heavyTile: {
			if (!isTileType(b) &&
				!isLaserEntityType(b) &&
				a->dP.y < 0 && 
				isEntityAbove(a, b)) result = false; 
		} break;

		case EntityType_pickupField: {
			result = !canPickupField(a, b, gameState);
		} break;
	}

	return result;
}

bool isSolidCollision(Entity* a, Entity* b, GameState* gameState) {
	bool result = isSolidCollisionRaw(a, b, gameState) && isSolidCollisionRaw(b, a, gameState);
	return result;
}

V2 moveRaw(Entity*, GameState*, V2, V2* ddP = 0);

void onCollide(Entity* entity, Entity* hitEntity, GameState* gameState, bool solid, V2 entityVel) {
	ConsoleField* killField = getField(entity, ConsoleField_killsOnHit); 
	bool killed = false;

	if(killField) {
		if(entity->type == EntityType_laserBeam ||
			hitEntity->type == EntityType_virus
		   || hitEntity->type == EntityType_text
		   || hitEntity->type == EntityType_flyingVirus 
		   //|| hitEntity->type == EntityType_player
		 ) {
		 	killed = true;
			setFlags(hitEntity, EntityFlag_remove);
		}
	}

	switch(entity->type) {
		case EntityType_pickupField: {
			if(canPickupField(entity, hitEntity, gameState)) {
				assert(!gameState->swapField);
				assert(entity->numFields >= 1);
				gameState->swapField = entity->fields[0];
				gameState->swapField->p -= gameState->camera.p;
				rebaseField(gameState->swapField, gameState->swapFieldP);

				removeField(entity->fields, entity->numFields, 0);
				entity->numFields--;
				setFlags(entity, EntityFlag_remove);
			}
		} break;

		case EntityType_hackEnergy: {
			if (hitEntity->type == EntityType_player) {
				setFlags(entity, EntityFlag_remove);
			}
		} break;



		case EntityType_endPortal: {
			if (hitEntity->type == EntityType_player) {
				gameState->loadNextLevel = true;
			}
		} break;



		case EntityType_laserBolt: {
			setFlags(entity, EntityFlag_remove);
		} break;



		case EntityType_laserBeam: {
			setFlags(hitEntity, EntityFlag_remove);
		} break;


		case EntityType_heavyTile: {
			if(!solid) {
				V2 moveDelta = v2(0, entityVel.y);
				V2 totalMovement = moveRaw(hitEntity, gameState, moveDelta);

				double squishY = entityVel.y - totalMovement.y;
				double squishFactor = (hitEntity->renderSize.y + squishY) / hitEntity->renderSize.y;

				hitEntity->renderSize.y += squishY;
				hitEntity->clickBox = scaleRect(hitEntity->clickBox, v2(1, squishFactor));

				bool hasNonZeroHitbox = false;

				Hitbox* hitboxList = hitEntity->hitboxes;

				while(hitboxList) {
					hitboxList->collisionSize.y += squishY;
					if (hitboxList->collisionSize.y > 0) hasNonZeroHitbox = true;
					hitboxList = hitboxList->next;
				}

				if (!hasNonZeroHitbox || hitEntity->renderSize.y <= 0) setFlags(hitEntity, EntityFlag_remove);

				moveRaw(hitEntity, gameState, v2(0, squishY));
			}
		} break;
	}

	if(!killed && killField && isSet(entity, EntityFlag_remove)) {
		setFlags(killField, ConsoleFlag_remove);
		removeFieldsIfSet(entity->fields, &entity->numFields);
	}
}

R2 getHitbox(Entity* entity, Hitbox* hitbox) {
	assert(hitbox);

	V2 offset = hitbox->collisionOffset;
	if (isSet(entity, EntityFlag_facesLeft)) offset.x *= -1;

	R2 result = rectCenterDiameter(entity->p + offset, hitbox->collisionSize);
	return result;
}

R2 getMaxCollisionExtents(Entity* entity) {
	R2 result = rectCenterRadius(entity->p, v2(0, 0));

	Hitbox* hitboxList = entity->hitboxes;

	while(hitboxList) {
		R2 hitbox = getHitbox(entity, hitboxList);

		if (hitbox.min.x < result.min.x) result.min.x = hitbox.min.x;
		if (hitbox.min.y < result.min.y) result.min.y = hitbox.min.y;
		if (hitbox.max.x > result.max.x) result.max.x = hitbox.max.x;
		if (hitbox.max.y > result.max.y) result.max.y = hitbox.max.y;

		hitboxList = hitboxList->next;
	}

	return result;
}

void getLineCollisionTime(double lx1, double lx2, double ly, double posX, double posY, 
						  double dPosX, double dPosY, bool top, 
						  GetCollisionTimeResult* result, bool* hit, bool* solidHit, bool horizontalCollision) {
	double collisionTime;

	double epsilon = 0.000000000001;

	if (dPosY == 0) {
		collisionTime = -1;
	} else {
		collisionTime = (ly - posY) / dPosY;

		if (collisionTime <= epsilon && ((dPosY > 0 && top) || (dPosY < 0 && !top))) {
			collisionTime = -1;
		} else {
			posX += dPosX * collisionTime;

			if (posX > lx1 && posX < lx2) {

			} else {
				collisionTime = -1;
			}
		}
	}

	if (collisionTime >= 0 && collisionTime < result->solidCollisionTime && horizontalCollision) {
		result->solidCollisionTime = collisionTime;
		result->solidHorizontalCollision = horizontalCollision;
		*solidHit = true;
	}

	if (collisionTime >= 0 && collisionTime < result->collisionTime) {
		result->collisionTime = collisionTime;
		result->horizontalCollision = horizontalCollision;
		*hit = true;
	}
}

void checkCollision(V2 delta, R2 colliderHitbox, Entity* entity, Entity* collider, Hitbox* colliderHitboxList, 
					Hitbox* entityHitboxList, GetCollisionTimeResult* result, GameState* gameState) {
	//Minkowski sum
	R2 sum = addDiameterTo(colliderHitbox, entityHitboxList->collisionSize);
	V2 center = entity->p + entityHitboxList->collisionOffset;

	bool hit = false;
	bool solidHit = false;

	if(pointInsideRectExclusive(sum, center)) {
		result->collisionTime = 0;
		//TODO: Need a third collision state for collisions that are neither horizontal or vertical
		result->horizontalCollision = true;
		hit = true;

		//TODO: Should this count as a solid collision?
		solidHit = true;
		result->solidHorizontalCollision = true;
		result->solidCollisionTime = 0;
	} else {
		getLineCollisionTime(sum.min.x, sum.max.x, sum.min.y, center.x, center.y, 
						 delta.x, delta.y, false, result, &hit, &solidHit, true);

		getLineCollisionTime(sum.min.x, sum.max.x, sum.max.y, center.x, center.y, 
							 delta.x, delta.y, true, result, &hit, &solidHit, true);

		getLineCollisionTime(sum.min.y, sum.max.y, sum.min.x, center.y, center.x, 
			 				 delta.y, delta.x, false, result, &hit, &solidHit, false);

		getLineCollisionTime(sum.min.y, sum.max.y, sum.max.x, center.y, center.x, 
	 						 delta.y, delta.x, true, result, &hit, &solidHit, false);
	}

	if (hit) {
		result->hitEntity = collider;
	}

	if (solidHit && isSolidCollision(entity, collider, gameState)) { 
		result->solidEntity = collider;
	}
}

bool isEntityInside(Entity* entity, Entity* collider)  {
	Hitbox* colliderHitboxList = collider->hitboxes;

	while(colliderHitboxList) {
		R2 colliderHitbox = getHitbox(collider, colliderHitboxList);

		Hitbox* entityHitboxList = entity->hitboxes;

		while (entityHitboxList) {
			V2 center = entity->p + entityHitboxList->collisionOffset;
			R2 sum = addDiameterTo(colliderHitbox, entityHitboxList->collisionSize);

			if(pointInsideRectExclusive(sum, center)) return true;

			entityHitboxList = entityHitboxList->next;
		}

		colliderHitboxList = colliderHitboxList->next;
	}

	return false;
}

void attemptToRemovePenetrationReferences(Entity* entity, GameState* gameState) {
	RefNode* node = entity->ignorePenetrationList;
	RefNode* prevNode = NULL;

	while(node) {
		Entity* collider = getEntityByRef(gameState, node->ref);
		bool removeRef = true;

		if(collider && isEntityInside(entity, collider)) {
			removeRef = false;
		}

		if(removeRef) {
			if(prevNode) prevNode->next = node->next;
			else entity->ignorePenetrationList = node->next;

			RefNode* nextPtr = node->next;

			node->next = NULL;
			freeRefNode(node, gameState);

			node = nextPtr;
		} else {
			prevNode = node;
			node = node->next;
		}

	}
}

void ignoreAllPenetratingEntities(Entity* entity, GameState* gameState) {
	//NOTE: Most of this code is pasted from checkCollision and getCollisionTime
	s32 partitionCenterX = (s32)(entity->p.x / gameState->chunkSize.x);
	s32 partitionCenterY = (s32)(entity->p.y / gameState->chunkSize.y);

	for (s32 partitionXOffs = -1; partitionXOffs <= 1; partitionXOffs++) {
		for (s32 partitionYOffs = -1; partitionYOffs <= 1; partitionYOffs++) {
			s32 partitionX = partitionCenterX + partitionXOffs;
			s32 partitionY = partitionCenterY + partitionYOffs;

			if (partitionX >= 0 && 
				partitionY >= 0 && 
				partitionX < gameState->chunksWidth && 
				partitionY < gameState->chunksHeight) {

				EntityChunk* chunk = gameState->chunks + partitionY * gameState->chunksWidth + partitionX;

				while(chunk) {
					for (s32 colliderIndex = 0; colliderIndex < chunk->numRefs; colliderIndex++) {
						Entity* collider = getEntityByRef(gameState, chunk->entityRefs[colliderIndex]);

						if (collider && collider != entity &&
							collidesWith(entity, collider, gameState)) {

							if(isEntityInside(entity, collider)) {
								RefNode* node = refNode(gameState, collider->ref, entity->ignorePenetrationList);
								entity->ignorePenetrationList = node;
							}
									
						}
					}
					chunk = chunk->next;
				}
			}
		}
	}
}

GetCollisionTimeResult getCollisionTime(Entity* entity, GameState* gameState, V2 delta) {
	GetCollisionTimeResult result = {};
	result.collisionTime = 1;
	result.solidCollisionTime = 1;

	s32 partitionCenterX = (s32)(entity->p.x / gameState->chunkSize.x);
	s32 partitionCenterY = (s32)(entity->p.y / gameState->chunkSize.y);

	for (s32 partitionXOffs = -1; partitionXOffs <= 1; partitionXOffs++) {
		for (s32 partitionYOffs = -1; partitionYOffs <= 1; partitionYOffs++) {
			s32 partitionX = partitionCenterX + partitionXOffs;
			s32 partitionY = partitionCenterY + partitionYOffs;

			if (partitionX >= 0 && 
				partitionY >= 0 && 
				partitionX < gameState->chunksWidth && 
				partitionY < gameState->chunksHeight) {

				EntityChunk* chunk = gameState->chunks + partitionY * gameState->chunksWidth + partitionX;

				while(chunk) {
					for (s32 colliderIndex = 0; colliderIndex < chunk->numRefs; colliderIndex++) {
						Entity* collider = getEntityByRef(gameState, chunk->entityRefs[colliderIndex]);

						if (collider && collider != entity &&
							collidesWith(entity, collider, gameState)) {

							// if (isTileType(collider) && isTileType(entity)) {
							// 	Hitbox tileHitbox = {};

							// 	double tileCollisionHeight = TILE_HEIGHT_WITHOUT_OVERHANG_IN_METERS;

							// 	tileHitbox.collisionSize = gameState->tileSize;//v2(gameState->tileSize.x, tileCollisionHeight);
							// 	//tileHitbox.collisionOffset = v2(0, -(gameState->tileSize.y - tileCollisionHeight) / 2.0);
							// 	R2 colliderHitbox = getHitbox(collider, &tileHitbox);
							// 	checkCollision(delta, colliderHitbox, entity, collider, &tileHitbox, &tileHitbox, &result, gameState);
							// } else {
								Hitbox* colliderHitboxList = collider->hitboxes;

								while(colliderHitboxList) {
									R2 colliderHitbox = getHitbox(collider, colliderHitboxList);

									Hitbox* entityHitboxList = entity->hitboxes;

									while (entityHitboxList) {
										checkCollision(delta, colliderHitbox, entity, collider,
											colliderHitboxList, entityHitboxList, &result, gameState);
										entityHitboxList = entityHitboxList->next;
									}

									colliderHitboxList = colliderHitboxList->next;
								}
							//}
						}
					}
					chunk = chunk->next;
				}
			}
		}
	}

	return result;
}

V2 getVelocity(double dt, V2 dP, V2 ddP) {
	V2 result = dt * dP + 0.5f * dt * dt * ddP;
	return result;
}

Entity* onGround(Entity* entity, GameState* gameState, double dt, V2 dP, V2 ddP) {
	V2 delta = getVelocity(dt, dP, ddP);
	GetCollisionTimeResult collisionResult = getCollisionTime(entity, gameState, delta);
	return collisionResult.solidEntity;
}

Entity* onGround(Entity* entity, GameState* gameState) {
	double moveTime = 1.0 / 60.0;

	//NOTE: This is so that ground references can still be made in 0 gravity
	V2 ddP = gameState->gravity;
	if(ddP.y == 0) ddP.y = -0.01;

	Entity* result = onGround(entity, gameState, moveTime, v2(0, 0), ddP);
	return result;
}

Entity* getAbove(Entity* entity, GameState* gameState) {
	double moveTime = 1.0 / 60.0;

	//NOTE: This is so that ground references can still be made in 0 gravity
	V2 ddP = -gameState->gravity;
	if(ddP.y == 0) ddP.y = 0.01;

	Entity* result = onGround(entity, gameState, moveTime, v2(0, 0), ddP);
	return result;
}

//Returns the total movement that is made
V2 moveRaw(Entity* entity, GameState* gameState, V2 delta, V2* ddP) {
	double maxCollisionTime = 1;
	V2 totalMovement = {};
	V2 totalMovementNoEpsilon = {};

	for (s32 moveIteration = 0; moveIteration < 4 && maxCollisionTime > 0; moveIteration++) {
		GetCollisionTimeResult collisionResult = getCollisionTime(entity, gameState, delta);
		double collisionTime = min(maxCollisionTime, collisionResult.collisionTime);

		maxCollisionTime -= collisionTime;

		double collisionTimeEpsilon = 0;//0.002;
		double moveTime = max(0, collisionTime - collisionTimeEpsilon); 

		V2 movement = delta * moveTime;
		setEntityP(entity, entity-> p + movement, gameState);
		totalMovement += movement;
		totalMovementNoEpsilon += delta * collisionTime;

		if (collisionResult.hitEntity) {
			bool solid = false;

			if (isSolidCollision(collisionResult.hitEntity, entity, gameState)) {
				solid = true;
    			if (collisionResult.horizontalCollision) {
					bool hitGround = false;

    				//NOTE: If moving and you would hit an entity which is using you as ground
    				//		then we try moving that entity first. Then retrying this move. 
    				//if (!isTileType(entity) || !isTileType(collisionResult.hitEntity)) {
    					RefNode* nextTopEntity = entity->groundReferenceList;
						RefNode* prevTopEntity = NULL;

	    				while (nextTopEntity) {
							if (nextTopEntity->ref == collisionResult.hitEntity->ref) {
								V2 pushAmount = maxCollisionTime * delta;
								moveRaw(collisionResult.hitEntity, gameState, pushAmount);

								if (prevTopEntity) {
									prevTopEntity->next = nextTopEntity->next;
								} else {
									entity->groundReferenceList = NULL;
								}

								nextTopEntity->next = NULL;
								freeRefNode(nextTopEntity, gameState);

								hitGround = true;
								break;
							}

							prevTopEntity = nextTopEntity;
							nextTopEntity = nextTopEntity->next;
						}
    				//}

					if (!hitGround) {
						delta.y = 0;
	    				entity->dP.y = 0;
	    				if (ddP) ddP->y = 0;
	    			} 
    			} else {
					delta.x = 0;
    				entity->dP.x = 0;
    				if (ddP) ddP->x = 0;
    			}
			}

			onCollide(entity, collisionResult.hitEntity, gameState, solid, maxCollisionTime * delta);
			onCollide(collisionResult.hitEntity, entity, gameState, solid, v2(0, 0));
		}
	}

	//NOTE: This moves all of the entities which are supported by this one (as the ground)
	RefNode* nextTopEntity = entity->groundReferenceList;

	if (lengthSq(totalMovementNoEpsilon) > 0) {
		while (nextTopEntity) {
			Entity* top = getEntityByRef(gameState, nextTopEntity->ref);
			if (top && !isSet(top, EntityFlag_movedByGround)) {
				setFlags(top, EntityFlag_movedByGround);
				moveRaw(top, gameState, totalMovementNoEpsilon);
			}
			nextTopEntity = nextTopEntity->next;
		}
	}

	return totalMovement;
}

V2 move(Entity* entity, double dt, GameState* gameState, V2 ddP) {
	V2 delta = getVelocity(dt, entity->dP, ddP);
	V2 movement = moveRaw(entity, gameState, delta, &ddP);
	entity->dP += ddP * dt;
	return movement;
}

bool moveTowardsTargetParabolic(Entity* entity, GameState* gameState, double dt, V2 target, double initialDstToTarget, double maxSpeed) {
	V2 delta = target - entity->p;
	double dstToTarget = length(delta);

	double percentageToTarget = (initialDstToTarget - dstToTarget) / initialDstToTarget;

	double initialTime = 0.1;
	double speedEquationCoefficient = maxSpeed / ((0.5 + initialTime) * (-0.5 - initialTime));
	double speed = dt * speedEquationCoefficient * (percentageToTarget + initialTime) * (percentageToTarget - 1 - initialTime);

	V2 movement;

	if (speed > dstToTarget) {
		movement = delta;
	} else {
		movement = speed * normalize(delta);
	}

	// if(entity->type == EntityType_tile) {
	// 	moveTile(entity, gameState, movement);
	// } else {
	V2 change = moveRaw(entity, gameState, movement);
	//}

	if (change.x && change.x < 0 != isSet(entity, EntityFlag_facesLeft)) {
		toggleFlags(entity, EntityFlag_facesLeft);
	}

	return entity->p == target;
}

bool moveTowardsWaypoint(Entity* entity, GameState* gameState, double dt, V2 target, double xMoveAcceleration) {
	V2 delta = target - entity->p;
	double deltaLen = length(delta);

	if (deltaLen > 0) {
		V2 ddP = normalize(delta) * xMoveAcceleration;

		V2 velocity = getVelocity(dt, entity->dP, ddP);
		double velocityLen = length(velocity);

		if(deltaLen <= velocityLen) {
			entity->dP = v2(0, 0);
			moveRaw(entity, gameState, delta);
		} else {
			move(entity, dt, gameState, ddP);
		}		

		if (delta.x && delta.x < 0 == isSet(entity, EntityFlag_facesLeft)) {
			toggleFlags(entity, EntityFlag_facesLeft);
		}

		return false;
	}

	return true;
}

ConsoleField* getField(ConsoleField** fields, int numFields, ConsoleFieldType type) {
	ConsoleField* result = NULL;

	for (s32 fieldIndex = 0; fieldIndex < numFields; fieldIndex++) {
		if (fields[fieldIndex]->type == type) {
			result = fields[fieldIndex];
			break;
		}
	}

	return result;
}

ConsoleField* getField(Entity* entity, ConsoleFieldType type) {
	ConsoleField* result = NULL;

	if(!isSet(entity, EntityFlag_remove)) {
		s32 firstFieldIndex = entity->type == EntityType_pickupField ? 1 : 0;
		result = getField(entity->fields + firstFieldIndex, entity->numFields - firstFieldIndex, type);
	}

	return result;
}

bool isMouseInside(Entity* entity, Input* input) {
	bool result = pointInsideRect(translateRect(entity->clickBox, entity->p), input->mouseInWorld);
	return result;
}

bool inSolidGridBounds(GameState* gameState, s32 xTile, s32 yTile) {
	bool result = xTile >= 0 &&
				  yTile >= 0 &&
				  xTile < gameState->solidGridWidth &&
				  yTile < gameState->solidGridHeight;

	return result;
}

void addSolidLocation(double xPos, double yPos, GameState* gameState) {
	s32 xTile = (s32)(xPos / gameState->solidGridSquareSize);
	s32 yTile = (s32)(yPos / gameState->solidGridSquareSize);

	if (inSolidGridBounds(gameState, xTile, yTile)) {
		gameState->solidGrid[xTile * gameState->solidGridHeight + yTile].solid = true;
	}
}

void testLocationAsClosestNonSolidNode(double xPos, double yPos, double* minDstSq, GameState* gameState, Entity* entity, PathNode** result) {
	s32 xTile = (s32)floor(xPos / gameState->solidGridSquareSize);
	s32 yTile = (s32)floor(yPos / gameState->solidGridSquareSize);

	if (inSolidGridBounds(gameState, xTile, yTile)) {
		PathNode* node = gameState->solidGrid + xTile * gameState->solidGridHeight + yTile;

		if (!node->solid) {
			double testDstSq = dstSq(node->p, entity->p);

			if (testDstSq < *minDstSq) {
				*minDstSq = testDstSq;
				*result = node;
			}
		}
	}
}

PathNode* getClosestNonSolidNode(Entity* entity, GameState* gameState, Entity* other = NULL) {
	PathNode* result = NULL;
	double minDstSq = 100000000000.0;

	Hitbox* hitboxList = entity->hitboxes;

	V2 otherSize;

	//TODO: Find a more robust way of doing this so entities don't get stuck
	s32 radiusFudge = 5;

	if(other) {
		R2 otherHitbox = getMaxCollisionExtents(other);
		otherSize = getRectSize(otherHitbox);
		radiusFudge = 1;
	}

	testLocationAsClosestNonSolidNode(entity->p.x, entity->p.y, &minDstSq, gameState, entity, &result);

	while (hitboxList) {
		R2 hitbox = getHitbox(entity, hitboxList);

		if(other) {
			hitbox = addDiameterTo(hitbox, otherSize);
		}

		V2 hitboxCenter = getRectCenter(hitbox);

		s32 radiusX = (s32)ceil(getRectWidth(hitbox) / (2 * gameState->solidGridSquareSize)) + radiusFudge;
		s32 radiusY = (s32)ceil(getRectHeight(hitbox) / (2 * gameState->solidGridSquareSize)) + radiusFudge;

		for(s32 xOffs = -radiusX; xOffs <= radiusX; xOffs++) {
			for (s32 yOffs = -radiusY; yOffs <= radiusY; yOffs++) {
				double xPos = hitboxCenter.x + xOffs * gameState->solidGridSquareSize;
				double yPos = hitboxCenter.y + yOffs * gameState->solidGridSquareSize;

				
				testLocationAsClosestNonSolidNode(xPos, yPos, &minDstSq, gameState, entity, &result);
			}
		}

		hitboxList = hitboxList->next;
	}

	return result;
}

bool pathLineClear(V2 p1, V2 p2, GameState* gameState) {
	double minX, maxX, minY, maxY;

	if (p1.x < p2.x) {
		minX = p1.x;
		maxX = p2.x;
	} else {
		minX = p2.x;
		maxX = p1.x;
	}

	if (p1.y < p2.y) {
		minY = p1.y;
		maxY = p2.y;
	} else {
		minY = p2.y;
		maxY = p1.y;
	}

	double increment = gameState->solidGridSquareSize / 6.0;
	V2 line = p2 - p1;
	double lineLength = length(p2 - p1);
	V2 delta = normalize(line);

	for(double linePos = 0; linePos < lineLength; linePos += increment) {
		V2 p = p1 + delta * linePos;

		s32 xTile = (s32)floor(p.x / gameState->solidGridSquareSize);
		s32 yTile = (s32)floor(p.y / gameState->solidGridSquareSize);

		if (inSolidGridBounds(gameState, xTile, yTile)) {
			if (gameState->solidGrid[xTile * gameState->solidGridHeight + yTile].solid) return false;
		}
	}

	s32 xTile = (s32)floor(maxX / gameState->solidGridSquareSize);
	s32 yTile = (s32)floor(maxY / gameState->solidGridSquareSize);	

	if (inSolidGridBounds(gameState, xTile, yTile)) {
		if (gameState->solidGrid[xTile * gameState->solidGridHeight + yTile].solid) return false;
	}

	return true;
}

V2 computePath(GameState* gameState, Entity* start, Entity* goal) {
	for (s32 tileX = 0; tileX < gameState->solidGridWidth; tileX++) {
		for (s32 tileY = 0; tileY < gameState->solidGridHeight; tileY++) {
			PathNode* node = gameState->solidGrid + tileX * gameState->solidGridHeight + tileY;
			node->solid = false;
			node->open = false;
			node->closed = false;
			node->parent = NULL;
			node->costToHere = 0;
			node->costToGoal = 0;
		}
	}

	gameState->openPathNodesCount = 0;

	for (s32 colliderIndex = 0; colliderIndex < gameState->numEntities; colliderIndex++) {
		Entity* collider = gameState->entities + colliderIndex;

		if (collider != start && collider != goal &&
			collidesWith(start, collider, gameState) &&
			collider->shooterRef != goal->ref) {

			Hitbox* colliderHitboxList = collider->hitboxes;

			while(colliderHitboxList) {
				R2 colliderHitbox = getHitbox(collider, colliderHitboxList);

				Hitbox* entityHitboxList = start->hitboxes;

				while (entityHitboxList) {
					R2 sum = addDiameterTo(colliderHitbox, entityHitboxList->collisionSize);
					sum = translateRect(sum, entityHitboxList->collisionOffset);

					for (double xPos = sum.min.x; xPos < sum.max.x; xPos += gameState->solidGridSquareSize) {
						for (double yPos = sum.min.y; yPos < sum.max.y; yPos += gameState->solidGridSquareSize) {
							addSolidLocation(xPos, yPos, gameState);
						}
					}

					for (double xPos = sum.min.x; xPos < sum.max.x; xPos += gameState->solidGridSquareSize) {
						addSolidLocation(xPos, sum.max.y, gameState);
					}

					for (double yPos = sum.min.y; yPos < sum.max.y; yPos += gameState->solidGridSquareSize) {
						addSolidLocation(sum.max.x, yPos, gameState);
					}

					addSolidLocation(sum.max.x, sum.max.y, gameState);

					entityHitboxList = entityHitboxList->next;
				}

				colliderHitboxList = colliderHitboxList->next;
			}
		}
	}



	if (pathLineClear(start->p, goal->p, gameState)) {
		return goal->p;
	}

	PathNode* startNode = getClosestNonSolidNode(start, gameState);
	PathNode* goalNode = getClosestNonSolidNode(goal, gameState, start);

	if (startNode && goalNode) {
		startNode->open = true;
		gameState->openPathNodes[0] = startNode;
		gameState->openPathNodesCount = 1;
		
		while(gameState->openPathNodesCount) {
			PathNode* current = NULL;
			s32 currentIndex = 0;
			double minEstimatedTotalCost = 1000000000000.0;

			for (s32 nodeIndex = 0; nodeIndex < gameState->openPathNodesCount; nodeIndex++) {
				PathNode* testNode = gameState->openPathNodes[nodeIndex];

				if (testNode->open) {
					double testEstimatedCost = testNode->costToHere + testNode->costToGoal;

					if (testEstimatedCost < minEstimatedTotalCost) {
						minEstimatedTotalCost = testEstimatedCost;
						current = testNode;
						currentIndex = nodeIndex;
					}
				}
			}

			assert(current);
			current->open = false;
			current->closed = true;
			gameState->openPathNodesCount--;

			if (gameState->openPathNodesCount) {
				gameState->openPathNodes[currentIndex] = gameState->openPathNodes[gameState->openPathNodesCount];
			} else {
				gameState->openPathNodes[0] = NULL;
			}

			if (current == goalNode) {
				V2 prevPoint = goal->p;
				PathNode* node = current;
				PathNode* prevNode = NULL;

				//NOTE: This is to smooth the path
				//TODO: Can optimize this by automatically removing co-linear points without checking pathLineClear
				while(node && node != startNode) {
					if (prevNode) {
						if (pathLineClear(node->p, prevNode->p, gameState)) {
							prevNode->parent = node->parent;
							node = prevNode;
						} else {
							prevNode = node;
						}
					} else {
						prevNode = node;
					}
					
					node = node->parent;
				}

				#if 0
				//NOTE: This draws the path
				node = current;
				setColor(gameState->renderer, 100, 75, 200, 255);

				while(node) {
					drawLine(gameState, prevPoint - gameState->cameraP, node->p - gameState->cameraP);
					prevPoint = node->p;
					node = node->parent;
				}
				#endif

				//TODO: Return path here
				node = current;
				PathNode* firstNode = NULL;
				PathNode* secondNode = NULL;

				while(node) {
					secondNode = firstNode;
					firstNode = node;
					node = node->parent;
				}

				if (secondNode) {
					return secondNode->p;
				}

				break;
			}

			for(s32 xOffs = -1; xOffs <= 1; xOffs++) {
				for (s32 yOffs = -1; yOffs <= 1; yOffs++) {
					if (xOffs != 0 || yOffs != 0) {
						s32 tileX = current->tileX + xOffs;
						s32 tileY = current->tileY + yOffs;

						if (inSolidGridBounds(gameState, tileX, tileY)) {
							PathNode* testNode = gameState->solidGrid + tileX * gameState->solidGridHeight + tileY;

							if (!testNode->solid) {
								#if 0
								if (current->parent) {
									int dx = current->tileX - current->parent->tileX;
									int dy = current->tileY - current->parent->tileY;

									if (dy == 0) {
										if (xOffs != dx ||
										    (yOffs == 1 && tileY + 1 < gameState->solidGridHeight &&
										    	!gameState->solidGrid[tileX][tileY + 1].solid) ||

											(yOffs == -1 && tileY + 1 >= 0 && 
												!gameState->solidGrid[tileX][tileY - 1].solid)) continue;
									}
									else if (dx == 0) {
										if (yOffs != dy ||
										   (xOffs == 1 && tileX + 1 < gameState->solidGridWidth && 
										   	!gameState->solidGrid[tileX + 1][tileY].solid) ||

										   (xOffs == -1 && tileX - 1 >= 0 &&
										   	!gameState->solidGrid[tileX - 1][tileY].solid)) continue;
									}
									else {
										if ((xOffs == -dx && tileX - dx >= 0 && tileX - dx < gameState->solidGridWidth &&
											!(yOffs == dy && gameState->solidGrid[tileX - dx][tileY].solid)) ||

										   ( yOffs == -dy && tileY - dy >= 0 && tileY - dy < gameState->solidGridHeight &&
										   	!(xOffs == dx && gameState->solidGrid[tileX][tileY - dy].solid)))
											continue;
									}
								}
								#endif
							
								double costToHere = current->costToHere;

								if(xOffs == 0 || yOffs == 0) {
									costToHere += 1;
								}
								else {
									costToHere += SQRT2;
								}

								if ((testNode->open || testNode->closed) && testNode->costToHere <= costToHere)
									continue;

								testNode->costToHere = costToHere;
								testNode->costToGoal = abs(goalNode->tileY - tileY) +
													   abs(goalNode->tileX - tileX);
								testNode->closed = false;
								testNode->parent = current;

								if (!testNode->open) {
									testNode->open = true;
									gameState->openPathNodes[gameState->openPathNodesCount++] = testNode;
									assert(gameState->openPathNodesCount < arrayCount(gameState->openPathNodes));
								}
							}
						}
					}
				}
			}
		}
	}

	return start->p;
}

void centerCameraAround(Entity* entity, GameState* gameState) {
	double maxCameraX = gameState->mapSize.x - gameState->windowSize.x;
	double x = clamp((double)(entity->p.x - gameState->windowSize.x / 2.0), 0, maxCameraX);

	gameState->camera.newP = v2(x, 0);
}

bool isTarget(Entity* entity, GameState* gameState) {
	bool result = refNodeListContainsRef(gameState->targetRefs, entity->ref);
	return result;
}

Entity* getClosestTarget(Entity* entity, GameState* gameState) {
	Entity* result = NULL;
	double minDstSq = 99999999999;

	RefNode* node = gameState->targetRefs;

	while(node) {
		Entity* testTarget = getEntityByRef(gameState, node->ref);

		if(testTarget && testTarget != entity) {
			double testDstSq = dstSq(entity->p, testTarget->p);

			if(testDstSq < minDstSq) {
				minDstSq = testDstSq;
				result = testTarget;
			}
		}

		node = node->next;
	}

	return result;
}

Entity* getClosestTargetInSight(Entity* entity, GameState* gameState, double sightRadius, double fov) {
	double halfFov = fov * 0.5;

	double minSightAngle = angleIn0360(entity->spotLightAngle - halfFov);
	double maxSightAngle = angleIn0360(entity->spotLightAngle + halfFov);

	RefNode* targetNode = gameState->targetRefs;
	Entity* target = NULL;
	double targetDst;

	while(targetNode && targetNode->ref != entity->ref) {
		Entity* testEntity = getEntityByRef(gameState, targetNode->ref);

		if(testEntity) {
			V2 toTestEntity = testEntity->p - entity->p;
			double dstToEntity = length(toTestEntity);

			if(dstToEntity <= sightRadius) {
				Hitbox* hitboxes = testEntity->hitboxes;
				bool canSee = false;

				double dir = getDegrees(toTestEntity);
				canSee |= isDegreesBetween(dir, minSightAngle, maxSightAngle);

				//TODO: Find a more robust way of determining if the entity is inside of the view arc
				while(hitboxes && !canSee) {
					V2 hitboxCenter = hitboxes->collisionOffset + testEntity->p - entity->p;

					V2 testP = hitboxCenter + hitboxes->collisionOffset;
					canSee |= isDegreesBetween(getDegrees(testP), minSightAngle, maxSightAngle);

					testP = hitboxCenter - hitboxes->collisionOffset;
					canSee |= isDegreesBetween(getDegrees(testP), minSightAngle, maxSightAngle);

					V2 offset = v2(hitboxes->collisionOffset.x, -hitboxes->collisionOffset.y);

					testP = hitboxCenter + offset;
					canSee |= isDegreesBetween(getDegrees(testP), minSightAngle, maxSightAngle);

					testP = hitboxCenter - offset;
					canSee |= isDegreesBetween(getDegrees(testP), minSightAngle, maxSightAngle);

					hitboxes = hitboxes->next;
				}

				if(canSee) {
					GetCollisionTimeResult collisionResult = getCollisionTime(entity, gameState, toTestEntity);

					bool occluded = (collisionResult.hitEntity && collisionResult.hitEntity != testEntity);

					if(!occluded) {
						if(!target || dstToEntity < targetDst) {
							targetDst = dstToEntity;
							target = testEntity;
						}
					}
				}
			} 
		}

		targetNode = targetNode->next;
	}

	return target;
}

void updateSpotlightBasedOnSpotlightField(Entity* entity, GameState* gameState) {
	ConsoleField* spotlightField = getField(entity, ConsoleField_spotlight);

	if(spotlightField) {
		ConsoleField* radiusField = spotlightField->children[0];
		double sightRadius = radiusField->doubleValues[radiusField->selectedIndex];

		ConsoleField* fovField = spotlightField->children[1];
		double fov = fovField->doubleValues[fovField->selectedIndex];

		SpotLight spotLight = createSpotLight(entity->p, v3(1, 1, .9), sightRadius, entity->spotLightAngle, fov);
		pushSpotLight(gameState->renderGroup, &spotLight, true);

		Entity* target = getClosestTargetInSight(entity, gameState, sightRadius, fov); 

		if(target) {
			entity->targetRef = target->ref;
		} else {
			entity->targetRef = 0;
		}
	}
}

bool shootBasedOnShootingField(Entity* entity, GameState* gameState, double dt) {
	ConsoleField* shootField = getField(entity, ConsoleField_shootsAtTarget);
	bool shootingEnabled = shootField && !gameState->doingInitialSim;
	bool shootingState = shootingEnabled;

	//NOTE:This handles shooting if the entity should shoot
	if (shootingEnabled) {
		if (isSet(entity, EntityFlag_unchargingAfterShooting)) {
			entity->shootTimer -= dt;

			if (entity->shootTimer <= 0) {
				entity->shootTimer = 0;
				clearFlags(entity, EntityFlag_unchargingAfterShooting);
			} 
		} else {
			assert(entity->targetRef != entity->ref);
			Entity* target = getEntityByRef(gameState, entity->targetRef); 

			if (target) {
				if (target->p.x < entity->p.x) setFlags(entity, EntityFlag_facesLeft);
				else clearFlags(entity, EntityFlag_facesLeft);
			}

			if(isSet(entity, EntityFlag_shooting)) {
				entity->shootTimer += dt;

				//TODO: Make shoot delay a child of the shoots console field?
				if (entity->shootTimer >= gameState->shootDelay) {
					entity->shootTimer = gameState->shootDelay;
					clearFlags(entity, EntityFlag_shooting);
					setFlags(entity, EntityFlag_unchargingAfterShooting);

					V2 spawnOffset = v2(0, 0);

					switch(entity->type) {
						case EntityType_virus:
							spawnOffset = v2(0, -0.3);
							break;
						case EntityType_flyingVirus:
							spawnOffset = v2(0.2, -0.29);
							break;
					}

					if (isSet(entity, EntityFlag_facesLeft)) {
						spawnOffset.x *= -1;
					}
					
					if(target) {
						ConsoleField* bulletSpeedField = shootField->children[0];
						double bulletSpeed = bulletSpeedField->doubleValues[bulletSpeedField->selectedIndex];

						addLaserBolt(gameState, entity->p + spawnOffset, target->p, entity->ref, bulletSpeed);
					}
				}
			} else { 
				if (target) {
					entity->shootTimer = 0;
					setFlags(entity, EntityFlag_shooting);
				} else {
					//No target was found (not in shooting state anymore)
					shootingState = false;
				}
			} 
		} 
	}

	return shootingState;
}

void moveEntityBasedOnMovementField(Entity* entity, GameState* gameState, double dt, double groundFriction, bool shootingState) {
	//NOTE: This is a hack to make the gravity feel better. There is more gravity when an object
	//		like the player is falling to make the gravity seem less 'floaty'.
	V2 gravity = gameState->gravity;
	if (entity->dP.y < 0) gravity *= 2;		
	V2 ddP;	


	ConsoleField* movementField = getMovementField(entity);
	
	if(affectedByGravity(entity, movementField)) {
		ddP = gravity;
	} else {
		ddP = {};
	}


	if (entity->type != EntityType_laserBolt) {
		entity->dP.x *= groundFriction;

		bool airFriction = (movementField && movementField->type == ConsoleField_seeksTarget);

		if (airFriction) {
			entity->dP.y *= groundFriction;
		}
	}

	bool defaultMove = true;


	//NOTE: This handles moving the entity
	if (movementField) {
		ConsoleField* speedField = movementField->children[0];
		double xMoveAcceleration = speedField->doubleValues[speedField->selectedIndex];

		defaultMove = false;

		double wantedSpotLightAngle = entity->spotLightAngle;

		switch(movementField->type) {




			case ConsoleField_keyboardControlled: {
				if (!gameState->doingInitialSim && !isSet(entity, EntityFlag_remove) && dt > 0) {
					wantedSpotLightAngle = entity->spotLightAngle = getDegrees(gameState->input.mouseInWorld - entity->p);

					double xMove = 0;

					ConsoleField* jumpField = movementField->children[1];
					double jumpHeight = jumpField->doubleValues[jumpField->selectedIndex];

					ConsoleField* doubleJumpField = movementField->children[2];
					bool canDoubleJump = doubleJumpField->selectedIndex != 0;

					if (gameState->input.right.pressed) xMove += xMoveAcceleration;
					if (gameState->input.left.pressed) xMove -= xMoveAcceleration;

					ddP.x += xMove;

					if (xMove > 0) {
						clearFlags(entity, EntityFlag_facesLeft);
					}
					else if (xMove < 0) {
						setFlags(entity, EntityFlag_facesLeft);
					}


					bool canJump = entity->jumpCount == 0 && 
								   entity->timeSinceLastOnGround < 0.15 && 
								   gameState->input.up.pressed;

					bool attemptingDoubleJump = false;
					
					if (!canJump) {
						attemptingDoubleJump = true;
						canJump = entity->jumpCount < 2 && canDoubleJump && gameState->input.up.justPressed;
					}

					if (canJump) {
						entity->dP.y = jumpHeight;
						if (attemptingDoubleJump) entity->jumpCount = 2;
						else entity->jumpCount++;
					}
				}

				move(entity, dt, gameState, ddP);

				//NOTE: This ensures the entity stays within the bounds of the map on x
				Hitbox* hitbox = entity->hitboxes;
				while(hitbox) {
					double minX = hitbox->collisionSize.x / 2;
					double maxX = gameState->mapSize.x - minX;

					double xPos = hitbox->collisionOffset.x + entity->p.x;

					if (xPos < minX) moveRaw(entity, gameState, v2(minX - xPos, 0));
					else if (xPos > maxX) moveRaw(entity, gameState, v2(maxX - xPos, 0));

					hitbox = hitbox->next;
				}
			} break;




			case ConsoleField_movesBackAndForth: {
				if (!shootingState && dt > 0) {
					V2 oldP = entity->p;

					if (isSet(entity, EntityFlag_facesLeft)) {
					 	xMoveAcceleration *= -1;
					} 

					ddP.x = xMoveAcceleration;

					double startX = entity->p.x;
					move(entity, dt, gameState, ddP);
					double endX = entity->p.x;

					bool shouldChangeDirection = false;

					if (isSet(entity, EntityFlag_noMovementByDefault)) {
						shouldChangeDirection = startX == endX;
					} else {
						bool32 initiallyOnGround = isSet(entity, EntityFlag_grounded);

						if (initiallyOnGround) {
							V2 oldCollisionSize = entity->hitboxes->collisionSize;
							V2 oldCollisionOffset = entity->hitboxes->collisionOffset;

							if (isSet(entity, EntityFlag_facesLeft)) {
							 	entity->hitboxes->collisionOffset.x -= entity->hitboxes->collisionSize.x / 2;
							} else {
								entity->hitboxes->collisionOffset.x += entity->hitboxes->collisionSize.x / 2;
							}

							entity->hitboxes->collisionSize.x = 0;

							bool offOfGround = onGround(entity, gameState) == NULL;

							entity->hitboxes->collisionSize = oldCollisionSize;
							entity->hitboxes->collisionOffset = oldCollisionOffset;

							if (offOfGround) {
								setEntityP(entity, oldP, gameState);
							}

							shouldChangeDirection = offOfGround || entity->dP.x == 0;
						}
					}

					if (shouldChangeDirection) {
						toggleFlags(entity, EntityFlag_facesLeft);
						entity->spotLightAngle = yReflectDegrees(entity->spotLightAngle);
					}
				} else {
					defaultMove = true;
				}
			} break;




			case ConsoleField_seeksTarget: {
				ConsoleField* alertnessField = getField(movementField->children, movementField->numChildren, ConsoleField_Alertness);
				assert(alertnessField);

				Alertness alertness = (Alertness)alertnessField->selectedIndex;

				if (!shootingState && !gameState->doingInitialSim && alertness > Alertness_asleep) {
					ConsoleField* sightRadiusField = movementField->children[1];
					double sightRadius = sightRadiusField->doubleValues[sightRadiusField->selectedIndex];

					//TODO: If obstacles were taken into account, this might not actually be the closest entity
					//		Maybe something like a bfs should be used here to find the actual closest entity
					Entity* targetEntity = getClosestTarget(entity, gameState);
					if (targetEntity) {
						double dstToTarget = dst(targetEntity->p, entity->p);

						if (dstToTarget <= sightRadius && dstToTarget > 0.1) {
							V2 wayPoint = computePath(gameState, entity, targetEntity);
							moveTowardsWaypoint(entity, gameState, dt, wayPoint, xMoveAcceleration);
						}
					}
				}
			} break;


			case ConsoleField_followsWaypoints: {
				ConsoleField* alertnessField = getField(movementField->children, movementField->numChildren, ConsoleField_Alertness);
				assert(alertnessField);

				Alertness alertness = (Alertness)alertnessField->selectedIndex;

				if(!shootingState && alertness > Alertness_asleep) {
					Waypoint* cur = movementField->curWaypoint;
					assert(cur);

					Waypoint* prev = cur;

					while(prev->next != cur) {
						prev = prev->next;
						assert(prev);
					}

					double initialDstToWaypoint = max(length(entity->p - cur->p), length(cur->p - prev->p));

					bool canMove = true;

					if(movementField->waypointDelay > 0) {
						movementField->waypointDelay += dt;

						assert(movementField->numChildren >= 3);
						ConsoleField* delayField = movementField->children[2];
						double delay = delayField->doubleValues[delayField->selectedIndex];

						if(movementField->waypointDelay >= delay) {
							movementField->waypointDelay = 0;
						} else {
							canMove = false;
						}
					}

					if(canMove) {
						if(moveTowardsTargetParabolic(entity, gameState, dt, cur->p, initialDstToWaypoint, xMoveAcceleration)) {
						movementField->curWaypoint = cur->next;
						movementField->waypointDelay += dt;
						}
					} else {
						defaultMove = true;
					}

					V2 toWaypoint = movementField->curWaypoint->p - entity->p;

					wantedSpotLightAngle = getDegrees(toWaypoint);
					double percentToTarget = (initialDstToWaypoint - length(toWaypoint)) / initialDstToWaypoint; 
					double sinInput = percentToTarget * TAU;

					double angleVariance = min(75, initialDstToWaypoint * 7);
					wantedSpotLightAngle += sin(sinInput) * angleVariance;
				}
			} break;



			case ConsoleField_bobsVertically: {
				ConsoleField* speedField = movementField->children[0];
				ConsoleField* maxBobHeightField = movementField->children[1];

				double speed = speedField->doubleValues[speedField->selectedIndex];
				double maxBobHeight = maxBobHeightField->doubleValues[maxBobHeightField->selectedIndex];

				V2 oldP = entity->p;

				double initialDstToTarget = movementField->initialBob ? maxBobHeight : maxBobHeight * 2;
				double maxBobHeightSigned = maxBobHeight * (movementField->bobbingUp * 2 - 1);
				V2 target = v2(entity->p.x, entity->p.y - movementField->bobHeight + maxBobHeightSigned);

				bool reachedTarget = moveTowardsTargetParabolic(entity, gameState, dt, target, initialDstToTarget, speed);

				double deltaHeight = entity->p.y - oldP.y;
				movementField->bobHeight += deltaHeight;

				if(reachedTarget) {
					movementField->bobbingUp = !movementField->bobbingUp;
					movementField->initialBob = false;
				} 
				else if (deltaHeight == 0 && dt) {
					movementField->bobbingUp = !movementField->bobbingUp;
					movementField->initialBob = false;
					movementField->bobHeight = 0;
				}
			} break;

		} //end of movement field switch 

		if(gameState->screenType != ScreenType_pause) {
			if(movementField->type != ConsoleField_followsWaypoints &&
			   movementField->type != ConsoleField_keyboardControlled) {
				wantedSpotLightAngle = getDegrees(entity->dP);
			}

			wantedSpotLightAngle = angleIn0360(wantedSpotLightAngle);
			
			double spotlightAngleMoveSpeed = 180 * dt;

			double clockwise = angleIn0360(wantedSpotLightAngle - entity->spotLightAngle);
			double counterClockwise = angleIn0360(entity->spotLightAngle - wantedSpotLightAngle);

			if(clockwise < counterClockwise) {
				double movement = min(clockwise, spotlightAngleMoveSpeed);
				entity->spotLightAngle = angleIn0360(entity->spotLightAngle + movement);
			} else {
				double movement = min(counterClockwise, spotlightAngleMoveSpeed);
				entity->spotLightAngle = angleIn0360(entity->spotLightAngle - movement);
			}
		}
	} 

	if(defaultMove) {
		if (!isSet(entity, EntityFlag_noMovementByDefault)) {
			if(entity->type == EntityType_laserBolt ||
			   entity->type == EntityType_heavyTile && entity->startPos != entity->p) {
				ddP.y = 0;
			} 

			move(entity, dt, gameState, ddP);
		}
	}

	if(getField(entity, ConsoleField_cameraFollows) && gameState->screenType == ScreenType_game && !getEntityByRef(gameState, gameState->consoleEntityRef)) {
		centerCameraAround(entity, gameState);
	}
}

void updateAndRenderEntities(GameState* gameState, double dtForFrame) {
	bool hacking = getEntityByRef(gameState, gameState->consoleEntityRef) != NULL;

	{
		Entity* player = getEntityByRef(gameState, gameState->playerRef);
		if(player && player->currentAnim.dataIndex == gameState->playerHack.dataIndex) hacking = true;
	}

	double dt = hacking ? 0 : dtForFrame;

	if(!gameState->doingInitialSim) {
		//TODO: It might be possible to combine the three loops which handle ground reference lists later
		//TODO: An entities ground reference list could be reset right after it is done being updated and rendered
		//NOTE: This is to reset all of the entities ground reference lists
		for (s32 entityIndex = 0; entityIndex < gameState->numEntities; entityIndex++) {
			Entity* entity = gameState->entities + entityIndex;

			entity->timeSinceLastOnGround += dt;
			clearFlags(entity, EntityFlag_grounded|EntityFlag_movedByGround);

			//NOTE: A ground reference between the base and the beam always persists.
			if(entity->type == EntityType_laserBase) {
				assert(entity->groundReferenceList);
				if (entity->groundReferenceList->next) {
					freeRefNode(entity->groundReferenceList->next, gameState);
					entity->groundReferenceList->next = NULL;
				}
			}
			else {
				if (entity->groundReferenceList) {
					freeRefNode(entity->groundReferenceList, gameState);
					entity->groundReferenceList = NULL;
				}
			}
		}

		//NOTE: This loops though all the entities to set if they are on the ground at the beginning of the frame
		//		and to setup their groundReferenceList's
		for (s32 entityIndex = 0; entityIndex < gameState->numEntities; entityIndex++) {
			Entity* entity = gameState->entities + entityIndex;

			Entity* above = getAbove(entity, gameState);

			if (above) {
				addGroundReference(above, entity, gameState);
			}

			Entity* below = onGround(entity, gameState);

			if (below) {
				addGroundReference(entity, below, gameState);
			}
		}
	} else {
		for (s32 entityIndex = 0; entityIndex < gameState->numEntities; entityIndex++) {
			Entity* entity = gameState->entities + entityIndex;
			clearFlags(entity, EntityFlag_movedByGround);
		}
	}

	double frictionGroundCoefficient = -15.0;
	double groundFriction = pow(E, frictionGroundCoefficient * dt);

	//NOTE: This loops through all of the entities in the game state to update and render them
	for (s32 entityIndex = 0; entityIndex < gameState->numEntities; entityIndex++) {
		Entity* entity = gameState->entities + entityIndex;

		removeFieldsIfSet(entity->fields, &entity->numFields);

		updateSpotlightBasedOnSpotlightField(entity, gameState);
		bool shootingState = shootBasedOnShootingField(entity, gameState, dt);
		moveEntityBasedOnMovementField(entity, gameState, dt, groundFriction, shootingState);

		bool insideLevel = false;

		//NOTE: This removes the entity if they are outside of the world
		if (isSet(entity, EntityFlag_removeWhenOutsideLevel)) {
			R2 world = r2(v2(0, 0), gameState->worldSize);

			Hitbox* hitboxList = entity->hitboxes;

			while(hitboxList) {
				R2 hitbox = getHitbox(entity, hitboxList);

				if (rectanglesOverlap(world, hitbox)) {
					insideLevel = true;
					break;
				}

				hitboxList = hitboxList->next;
			}

			
		} else {
			insideLevel = true;
		}

		//NOTE: Remove entity if it is below the bottom of the world
		//The laser base and laser beams are not removed when the bottom is reached because their ground references
		//are used to pull down the laser top.
		//TODO: The laser base still needs to be removed somewhere though (once both hitboxes are outside of the level)
		if(entity->type != EntityType_laserBase) {
			if((gameState->gravity.y <= 0 && entity->p.y < -entity->renderSize.y / 2) ||
			  (gameState->gravity.y >= 0 && entity->p.y > gameState->windowSize.y + entity->renderSize.y / 2)) {
				insideLevel = false;
			}
		}

		if (!insideLevel) {
			bool removeFields = false;

			for(s32 fieldIndex = 0; fieldIndex < entity->numFields; fieldIndex++) {
				ConsoleField* field = entity->fields[fieldIndex];

				if(field->type == ConsoleField_killsOnHit || field->type == ConsoleField_givesEnergy) {
					removeFields = true;
					setFlags(field, ConsoleFlag_remove);
				}
			}

			if(removeFields) removeFieldsIfSet(entity->fields, &entity->numFields);

			setFlags(entity, EntityFlag_remove);
		}

		//NOTE: Individual entity logic here
		switch(entity->type) {
			 case EntityType_player: {
				if (!isSet(entity, EntityFlag_remove) && !gameState->doingInitialSim) {
					gameState->playerDeathStartP = entity->p;
					gameState->playerDeathSize = entity->renderSize;
				}
			} break;
		


			case EntityType_background: {
				BackgroundTexture* backgroundTexture = getBackgroundTexture(&gameState->backgroundTextures);
				drawBackgroundTexture(backgroundTexture, gameState->renderGroup, &gameState->camera, gameState->windowSize, gameState->mapSize.x);				
			} break;


			case EntityType_heavyTile:
			case EntityType_tile: {
				assert(entity->numFields >= 2);
				assert(entity->fields[0]->type == ConsoleField_unlimitedInt && 
					   entity->fields[1]->type == ConsoleField_unlimitedInt);

				s32 fieldXOffset = entity->fields[0]->selectedIndex;
				s32 fieldYOffset = entity->fields[1]->selectedIndex;

				if(getMovementField(entity)) {
					entity->tileXOffset = fieldXOffset;
					entity->tileYOffset = fieldYOffset;
				}

				s32 dXOffset = fieldXOffset - entity->tileXOffset;
				s32 dYOffset = fieldYOffset - entity->tileYOffset;

				if (dXOffset != 0 || dYOffset != 0) {
					V2 tileSize = v2(gameState->tileSize.x, TILE_HEIGHT_WITHOUT_OVERHANG_IN_METERS);

					V2 target = entity->startPos + hadamard(v2((double)dXOffset, (double)dYOffset), 
						tileSize/*getRectSize(entity->clickBox)*/);

					//double initialDst = dXOffset == 0 ? getRectHeight(entity->clickBox) : getRectWidth(entity->clickBox);

					double initialDstTest = dXOffset != 0 ? tileSize.x : tileSize.y;
					double initialDst = max(initialDstTest, length(target - entity->p));

					if (moveTowardsTargetParabolic(entity, gameState, dtForFrame, target, initialDst, 6.0f)) {
						entity->tileXOffset = fieldXOffset;
						entity->tileYOffset = fieldYOffset;
					}
				} else {
					entity->startPos = entity->p;
				}

			} break;



			case EntityType_text: {
				assert(entity->numFields >= 1);
				assert(entity->fields[0]->type == ConsoleField_s32);

				s32 selectedIndex = entity->fields[0]->s32Values[entity->fields[0]->selectedIndex];
				setSelectedText(entity, selectedIndex, gameState);
			} break;




			case EntityType_playerDeath: {
				double initialDstToTarget = length(gameState->playerDeathStartP - gameState->playerStartP);
				double maxSpeed = 7.5 * cbrt(initialDstToTarget);

				if (moveTowardsTargetParabolic(entity, gameState, dt, gameState->playerStartP, 
										initialDstToTarget, maxSpeed)) {
					setFlags(entity, EntityFlag_remove);
					Entity* player = addPlayer(gameState, gameState->playerStartP - 0.5 * entity->renderSize);
					setFlags(player, EntityFlag_grounded);
					player->dP = v2(0, 0);
				}

				centerCameraAround(entity, gameState);
			} break;



			case EntityType_laserBase: {
				assert(entity->numFields >= 1);
				assert(entity->fields[0]->type == ConsoleField_bool);

				bool laserOn = (entity->fields[0]->selectedIndex != 0);

				Entity* beam = getEntityByRef(gameState, entity->ref + 1);
				assert(!beam || beam->type == EntityType_laserBeam);

				Texture* topTexture = NULL;
				Texture* baseTexture = NULL;

				if(laserOn) {
					setFlags(entity, EntityFlag_laserOn);

					if (beam) setFlags(beam, EntityFlag_laserOn);

					topTexture = &gameState->laserTopOn;
					baseTexture = &gameState->laserBaseOn;
				}
				else {
					clearFlags(entity, EntityFlag_laserOn);

					if (beam) clearFlags(beam, EntityFlag_laserOn);

					topTexture = &gameState->laserTopOff;
					baseTexture = &gameState->laserBaseOff;
				}
	
				assert(topTexture && baseTexture);

				R2 baseBounds = rectCenterDiameter(entity->p, entity->renderSize);

				//NOTE: entity->hitboxes->collisionOffset is the height of the laser entity
				R2 topBounds = translateRect(baseBounds, entity->hitboxes->collisionOffset);

				pushTexture(gameState->renderGroup, topTexture, topBounds, false, entity->drawOrder, true, Orientation_0, createColor(255, 255, 255, 255), entity->emissivity);
				pushTexture(gameState->renderGroup, baseTexture, baseBounds, false, entity->drawOrder, true, Orientation_0, createColor(255, 255, 255, 255), entity->emissivity);

				//pushFilledRect(gameState->renderGroup, topBounds, createColor(255, 0, 0, 100), true);
				//pushFilledRect(gameState->renderGroup, baseBounds, createColor(255, 0, 0, 100), true);
			} break;



			case EntityType_pickupField: {
				//NOTE: If the pickupField has been removed and the field is transferred to the swap field
				//		then it will have 1 field (the solid field)
				if(entity->numFields >= 1 && !isSet(entity, EntityFlag_remove)) {
					bool32 wasSolid = isSet(entity, EntityFlag_wasSolid);
					if(isPickupFieldSolid(entity) != wasSolid) {
						toggleFlags(entity, EntityFlag_wasSolid);

						if(!wasSolid) ignoreAllPenetratingEntities(entity, gameState);
					}

					if(gameState->consoleEntityRef != entity->ref) {
						entity->fields[0]->p = entity->p;
						pushConsoleField(gameState->renderGroup, &gameState->fieldSpec, entity->fields[0]);
					}
				}
			} break;

		} //End of switch statement on entity type



		//NOTE: This handles rendering all the entities
		bool showPlayerHackingAnim = entity->type == EntityType_player && getEntityByRef(gameState, gameState->consoleEntityRef);
		CharacterAnimData* characterAnim = NULL;

		if(entity->characterAnim.dataIndex) {
			characterAnim = getCharacterAnimData(entity->characterAnim, gameState);
		}

		if (entity->type == EntityType_player && hacking) {
			entity->animTime += dtForFrame;
		} else {
			entity->animTime += dt;
		}

		if(characterAnim) {
			if(isSet(entity, EntityFlag_animIntro)) {
				assert(!isSet(entity, EntityFlag_animOutro));
				assert(entity->currentAnim.dataIndex);

				AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);
				assert(currentAnim);
				assert(currentAnim->intro.frames);
				double duration = getAnimationDuration(&currentAnim->intro);

				if(entity->animTime >= duration) {
					clearFlags(entity, EntityFlag_animIntro);
					entity->animTime -= duration;
				}
			}

			else if(isSet(entity, EntityFlag_animOutro)) {
				assert(entity->currentAnim.dataIndex && entity->nextAnim.dataIndex);

				AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);
				assert(currentAnim->outro.frames);

				double duration = getAnimationDuration(&currentAnim->outro);

				if(entity->animTime >= duration) {
					clearFlags(entity, EntityFlag_animOutro);
					entity->animTime -= duration;
					entity->currentAnim = entity->nextAnim;
				}
			}

			else if(entity->currentAnim.dataIndex) {
				AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);
				assert(currentAnim->main.frames);

				double duration = getAnimationDuration(&currentAnim->main);
				//if(entity->currentAnim->main.pingPong) duration *= 2;

				if(duration <= 0) {
					entity->animTime = 0;
				}
			}

			if(showPlayerHackingAnim) {
				entity->nextAnim = gameState->playerHack;
			}
			else if(shootingState && characterAnim->shootAnim.dataIndex) {
				entity->nextAnim = characterAnim->shootAnim;
			}
			else if((entity->dP.y != 0 || !isSet(entity, EntityFlag_grounded)) && characterAnim->jumpAnim.dataIndex) {
				entity->nextAnim = characterAnim->jumpAnim;
			}
			else if(abs(entity->dP.x) > 0.1f && characterAnim->walkAnim.dataIndex) {
				entity->nextAnim = characterAnim->walkAnim;
			}
			else if (characterAnim->standAnim.dataIndex) {
				entity->nextAnim = characterAnim->standAnim;
			}

			if(entity->nextAnim.dataIndex == entity->currentAnim.dataIndex) {
				entity->nextAnim.dataIndex = 0;
			}

			if (entity->nextAnim.dataIndex) {
				if(isSet(entity, EntityFlag_animIntro)) {
					assert(entity->currentAnim.dataIndex);
					AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);
					assert(currentAnim);
					assert(currentAnim->intro.frames);
					if(currentAnim->intro.frames &&
					    currentAnim->intro.frames == currentAnim->outro.frames) {
						clearFlags(entity, EntityFlag_animIntro);
						setFlags(entity, EntityFlag_animOutro);
						entity->animTime = getAnimationDuration(&currentAnim->intro) - entity->animTime;
					}
				} else if(!isSet(entity, EntityFlag_animOutro)) {
					bool transitionToOutro = true;
					bool transitionToNext = (entity->nextAnim.dataIndex == characterAnim->jumpAnim.dataIndex);

					if(entity->currentAnim.dataIndex){
						AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);
						assert(currentAnim);

						if(currentAnim->finishMainBeforeOutro) {
							transitionToOutro = (entity->animTime >= getAnimationDuration(&currentAnim->main));
						}
					}

					if(transitionToOutro) {
						entity->animTime = 0;

						if(entity->currentAnim.dataIndex) {
							AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);
							assert(currentAnim);

							if(currentAnim->outro.numFrames) {
								setFlags(entity, EntityFlag_animOutro);
							} else {
								transitionToNext = true;
							}
						}
						else {
							transitionToNext = true;
						}
					}

					if(transitionToNext) {
						clearFlags(entity, EntityFlag_animOutro);
						entity->animTime = 0;
						entity->currentAnim = entity->nextAnim;
						assert(entity->currentAnim.dataIndex);

						AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);
						assert(currentAnim);

						if(currentAnim->intro.frames) {
							setFlags(entity, EntityFlag_animIntro);
						} else {
							clearFlags(entity, EntityFlag_animIntro);
						}
					}
				}
			} else {
				clearFlags(entity, EntityFlag_animOutro);
			}

			AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);

			if((!entity->nextAnim.dataIndex || !entity->currentAnim.dataIndex || !currentAnim->outro.frames) && 
				isSet(entity, EntityFlag_animOutro)) {
				InvalidCodePath;
			}

			if((!entity->currentAnim.dataIndex || !currentAnim->intro.frames) && 
				isSet(entity, EntityFlag_animIntro)) {
				InvalidCodePath;
			}
		}

		TextureData* texture = NULL;
		Texture* tempTexture = NULL;

		if(entity->currentAnim.dataIndex) {
			Animation* anim = NULL;

			AnimNodeData* currentAnim = getAnimNodeData(entity->currentAnim, gameState);
			assert(currentAnim);

			if(isSet(entity, EntityFlag_animIntro)) {
				anim = &currentAnim->intro;
			}
			else if(isSet(entity, EntityFlag_animOutro)) {
				anim = &currentAnim->outro;
			}
			else {
				anim = &currentAnim->main;
			}

			assert(anim);
			tempTexture = getAnimationFrame(anim, entity->animTime);

			assert(tempTexture);
			assert(tempTexture->dataIndex);
		}
		else if(entity->defaultTex.dataIndex) {
			tempTexture = &entity->defaultTex;
		}
		else if(entity->messages) {
			texture = &entity->messages->textures[entity->messages->selectedIndex];
		} else if(entity->type == EntityType_hackEnergy) {
			tempTexture = getAnimationFrame(&gameState->hackEnergyAnim, entity->animTime);
		}

		if(tempTexture) {
			texture = getTextureData(tempTexture, gameState->renderGroup);
			assert(texture);
		}

		if (texture != NULL) {
			assert(texture->texId);

			bool drawLeft = isSet(entity, EntityFlag_facesLeft) != 0;
			if (entity->type == EntityType_laserBase) drawLeft = false;

			if(entity->currentAnim.dataIndex == gameState->playerHack.dataIndex && !isSet(entity, EntityFlag_animIntro|EntityFlag_animOutro)) {
				V2 boundsIncrease = v2(entity->renderSize.x, 0);
				V2 offset = boundsIncrease * 0.5 * (drawLeft ? -1 : 1);
				R2 bounds = rectCenterDiameter(entity->p + offset, entity->renderSize + boundsIncrease);

				pushTexture(gameState->renderGroup, texture, bounds, drawLeft, entity->drawOrder, true);
			}
			else if (entity->type != EntityType_laserBeam || isSet(entity, EntityFlag_laserOn)) {
				DrawOrder drawOrder = entity->drawOrder;

				if(entity->type == EntityType_tile && getMovementField(entity) != NULL) drawOrder = DrawOrder_movingTile;
				pushEntityTexture(gameState->renderGroup, texture, entity, drawLeft, drawOrder);
			}
		}

		#if SHOW_COLLISION_BOUNDS
			Hitbox* hitbox = entity->hitboxes;

			while (hitbox) {
				pushOutlinedRect(gameState->renderGroup, getHitbox(entity, hitbox),
								 0.02f, createColor(255, 127, 255, 255), true);
				hitbox = hitbox->next;
			}

		#endif

		#if SHOW_CLICK_BOUNDS
			if(isSet(entity, EntityFlag_hackable)) {
				R2 clickBox = translateRect(entity->clickBox, entity->p);
				pushOutlinedRect(gameState->renderGroup, clickBox, 0.02f, createColor(127, 255, 255, 255), true);
			}
		#endif
	}
}
