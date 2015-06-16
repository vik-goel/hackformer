void freeConsoleField(ConsoleField* field, GameState* gameState) {
	// if (field->next) {
	// 	freeConsoleField(field->next, gameState);
	// 	field->next = NULL;
	// }

	//NOTE: Since the next pointer is only used in the free list and consoles in the free list should
	//		never be freed again, the next pointer should be null. 
	assert(!field->next);

	for (s32 childIndex = 0; childIndex < field->numChildren; childIndex++) {
		freeConsoleField(field->children[childIndex], gameState);
		field->children[childIndex] = NULL;
	}

	field->next = gameState->consoleFreeList;
	gameState->consoleFreeList = field;
}

ConsoleField* createConsoleField_(GameState* gameState) {
	ConsoleField* result = NULL;

	if (gameState->consoleFreeList) {
		result = gameState->consoleFreeList;
		gameState->consoleFreeList = gameState->consoleFreeList->next;
	} else {
		result = (ConsoleField*)pushStruct(&gameState->levelStorage, ConsoleField);
	}

	return result;
}

ConsoleField* createConsoleField(GameState* gameState, ConsoleField* copy) {
	ConsoleField* result = createConsoleField_(gameState);
	*result = *copy;
	return result;
}

ConsoleField* createConsoleField(GameState* gameState, char* name, ConsoleFieldType type, s32 tweakCost) {
	ConsoleField* result = createConsoleField_(gameState);

	*result = {};
	result->type = type;
	assert(strlen(name) < arrayCount(result->name));
	strcpy(result->name, name);
	result->tweakCost = tweakCost;

	return result;
}

ConsoleField* createUnlimitedIntField(GameState* gameState, char* name, s32 value, s32 tweakCost) {
	ConsoleField* result = createConsoleField(gameState, name, ConsoleField_unlimitedInt, tweakCost);

	result->selectedIndex = result->initialIndex = value;

	return result;
}

#define createEnumField(enumName, gameState, name, value, tweakCost) createEnumField_(gameState, name, value, enumName##_count, tweakCost, ConsoleField_##enumName)
ConsoleField* createEnumField_(GameState* gameState, char* name, s32 value, s32 numValues, s32 tweakCost, ConsoleFieldType type) {
	ConsoleField* result = createConsoleField(gameState, name, type, tweakCost);
	result->initialIndex = result->selectedIndex = value;
	result->numValues = numValues;
	return result;
}

ConsoleField* createBoolField(GameState* gameState, char* name, bool value, s32 tweakCost) {
	ConsoleField* result = createConsoleField(gameState, name, ConsoleField_bool, tweakCost);

	result->initialIndex = result->selectedIndex = value ? 1 : 0;
	result->numValues = 2;

	return result;
}

#define createPrimitiveField(type, gameState, name, values, numValues, initialIndex, tweakCost) createPrimitiveField_(gameState, name, values, sizeof(type), numValues, initialIndex, tweakCost, ConsoleField_##type)
ConsoleField* createPrimitiveField_(GameState* gameState, char* name, void* values, s32 elemSize,
				s32 numValues, s32 initialIndex, s32 tweakCost, ConsoleFieldType type) {
	assert(numValues > 0);
	assert(initialIndex >= 0 && initialIndex < numValues);

	ConsoleField* result = createConsoleField(gameState, name, type, tweakCost);

	for (s32 valueIndex = 0; valueIndex < numValues; valueIndex++) {
		char* src = (char*)values + elemSize * valueIndex;
		char* dst = (char*)result->doubleValues + elemSize * valueIndex;

		for(s32 byteIndex = 0; byteIndex < elemSize; byteIndex++) {
			*dst++ = *src++;
		}
	}

	result->selectedIndex = result->initialIndex = initialIndex;
	result->numValues = numValues;

	return result;
}

bool hasValues(ConsoleField* field) {
	bool result = field->numValues || field->type == ConsoleField_unlimitedInt;
	return result;
}

double getTotalFieldHeight(FieldSpec* spec, ConsoleField* field) {
	double result = spec->fieldSize.y + spec->spacing.y;
	if(hasValues(field)) result += spec->valueSize.y - spec->valueBackgroundPenetration;
	return result;
}

s32 getNumVisibleFields(ConsoleField** fields, s32 fieldsCount) {
	s32 result = fieldsCount;

	for (s32 fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];

		if (field->numChildren && isSet(field, ConsoleFlag_childrenVisible)) {
			result += getNumVisibleFields(field->children, field->numChildren);
		}
	}

	return result;
}

double getMaxChildYOffset(ConsoleField* field, FieldSpec* spec) {
	double result = 0;

	for(s32 childIndex = 0; childIndex < field->numChildren; childIndex++) {
		ConsoleField* child = field->children[childIndex];
		result += getTotalFieldHeight(spec, child);
	}

	return result;
}

double getTotalYOffset(ConsoleField** fields, s32 fieldsCount, FieldSpec* spec, bool isPickupField) {
	double result = 0;

	for (s32 fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];
		result += getTotalFieldHeight(spec, field);
	}

	//NOTE: The 0th fieldIndex is skipped when getting the y offset of a pickup field
	//		because those fields can go below where the entity is. 
	for (s32 fieldIndex = isPickupField; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];
		result += field->childYOffs;
	}

	return result; 
}

void moveToEquilibrium(ConsoleField* field, double dt) {
	if(isSet(field, ConsoleFlag_selected)) return;

	V2 delta = field->offs;
	double len = length(delta);
	double maxSpeed = 1 * dt + len / 10;

	if (len) {
		if (len > maxSpeed) {
			delta = normalize(delta) * maxSpeed;
		}

		field->offs -= delta;
	}
}

void rebaseField(ConsoleField* field, V2 newBase) {
	field->offs = field->p - newBase;
}

R2 getRenderBounds(Entity* entity, GameState* gameState) {
	R2 result = translateRect(entity->clickBox, entity->p - gameState->camera.p);
	return result;
}

V2 getBottomFieldP(Entity* entity, GameState* gameState, FieldSpec* spec) {
	R2 renderBounds = getRenderBounds(entity, gameState);

	V2 entityScreenP = (entity->p - gameState->camera.p) * gameState->pixelsPerMeter;
	bool onScreenRight = entityScreenP.x >= gameState->windowWidth / 2;

	bool isPickupField = entity->type == EntityType_pickupField;
	if(isPickupField) onScreenRight = true;

	//TODO: Get better values for these
	V2 fieldOffset;
	if (isPickupField) {
		fieldOffset = v2(spec->fieldSize.x * -0.75, 0);
	} else {
		fieldOffset = v2(spec->fieldSize.x * 0.75, 0);
	}



	if(entity->type == EntityType_player && !onScreenRight && !isSet(entity, EntityFlag_facesLeft)) {
		fieldOffset.x += getRectWidth(renderBounds) * 1.5;
	}

	V2 result = fieldOffset + getRectCenter(renderBounds);
	return result;
}

void onAddConsoleFieldToEntity(Entity* entity, ConsoleField* field, bool exchanging, GameState* gameState) {
	switch(entity->type) {
		case EntityType_text: {
			if(!exchanging && isConsoleFieldMovementType(field)) {
				ignoreAllPenetratingEntities(entity, gameState);
			}
		} break;
	}

	if(field->type == ConsoleField_cameraFollows) {
		setCameraTarget(&gameState->camera, entity->p);
	}
}

bool canFieldBeCloned(ConsoleField* field) {
	bool result = !(field->type == ConsoleField_cameraFollows ||
					field->type == ConsoleField_givesEnergy);

	return result;
}

bool moveField(ConsoleField* field, GameState* gameState, double dt, FieldSpec* spec) {
	assert(!hasValues(field));

	bool result = false;

	R2 bounds = rectCenterDiameter(field->p, spec->fieldSize);

	if (gameState->input.leftMouse.justPressed) {
		if (pointInsideRect(bounds, gameState->input.mouseInMeters)) {
			if(!isSet(field, ConsoleFlag_selected)) {
				if(gameState->input.shift.pressed) {
					if(!gameState->swapField && canFieldBeCloned(field)) {
						if(spec->blueEnergy >= field->tweakCost) {
							spec->blueEnergy -= field->tweakCost;
							gameState->swapField = createConsoleField(gameState, field);
							rebaseField(gameState->swapField, gameState->swapFieldP); 
						}
					}
				} else {
					spec->mouseOffset = (gameState->input.mouseInMeters - field->p);
					setFlags(field, ConsoleFlag_selected);
				}
			}

			result = true;
		}
	}

	if (isSet(field, ConsoleFlag_selected)) {
		if (gameState->input.leftMouse.pressed) {
			field->offs += gameState->input.mouseInMeters - field->p - spec->mouseOffset;
		} else {
			Entity* consoleEntity = getEntityByRef(gameState, gameState->consoleEntityRef);

			if(consoleEntity) {
				double dstSqToConsoleEntity = dstSq(getBottomFieldP(consoleEntity, gameState, spec), field->p);
				double dstSqToSwapField = dstSq(gameState->swapFieldP, field->p);

				bool addToParent = false;
				bool removeFromParent = false;
				bool removeFromSwapField = false;
				bool addToSwapField = false;
				bool exchangeFields = false;

				if (gameState->swapField == field) {
					if (dstSqToConsoleEntity < dstSqToSwapField) {
						removeFromSwapField = true;
						addToParent = true;
					}
				} else {
					if (dstSqToSwapField < dstSqToConsoleEntity) {
						if (gameState->swapField == NULL) {
							addToSwapField = true;
							removeFromParent = true;
						}
						else if (isConsoleFieldMovementType(gameState->swapField) && isConsoleFieldMovementType(field)) {
							exchangeFields = true;
						}
					}
				}

				assert(!(addToParent && removeFromParent));
				assert(!(addToSwapField && removeFromSwapField));
				assert(!(exchangeFields && (addToParent || removeFromParent || addToSwapField || removeFromSwapField)));

				bool exchangeChangedField = false;

				if (gameState->swapField == field && isConsoleFieldMovementType(field)) {
					if (addToParent) {
						ConsoleField* movementField = getMovementField(consoleEntity);

						if (movementField) {
							field = movementField;
							addToParent = removeFromParent = addToSwapField = removeFromSwapField = false;
							exchangeFields = true;
							exchangeChangedField = true;
						}
					}
				}

				if(addToParent) {
					bool alreadyHaveField = canOnlyHaveOneFieldOfType(field->type) && getField(consoleEntity, field->type);
					if(alreadyHaveField) {
						addToParent = false;
						assert(removeFromSwapField);
						removeFromSwapField = false;
					}
				}

				s32 totalCost = 0;
				bool movementAllowed = true;

				if(exchangeFields || removeFromSwapField) {
					assert(gameState->swapField);
					assert(gameState->swapField->tweakCost > 0);
					totalCost += gameState->swapField->tweakCost;
				}
				if(exchangeFields || removeFromParent) {
					assert(field);

					if(field->tweakCost > 0) {
						totalCost += field->tweakCost;
					} else {
						movementAllowed = false;
					}
				}

				if(spec->blueEnergy < totalCost) movementAllowed = false;

				if(movementAllowed) {
					spec->blueEnergy -= totalCost;

					if (field->type == ConsoleField_isShootTarget) {
						if (removeFromParent) removeTargetRef(gameState->consoleEntityRef, gameState);
						else if (addToParent) addTargetRef(gameState->consoleEntityRef, gameState);
					}

					if(exchangeFields) {
						ConsoleField* swapField = gameState->swapField;

						V2 fieldP = field->p;
						V2 fieldOffs = field->offs;

						gameState->swapField = field;
						rebaseField(gameState->swapField, gameState->swapFieldP);

						bool exchanged = false;

						for(s32 fieldIndex = 0; fieldIndex < consoleEntity->numFields; fieldIndex++) {
							ConsoleField* f = consoleEntity->fields[fieldIndex];

							if(f == field) {
								consoleEntity->fields[fieldIndex] = swapField;

								if(exchangeChangedField) {
									rebaseField(consoleEntity->fields[fieldIndex], field->p);
								} else {
									rebaseField(consoleEntity->fields[fieldIndex], fieldP - fieldOffs);	
								}

								exchanged = true;
								break;
							}
						}

						assert(exchanged);

						onAddConsoleFieldToEntity(consoleEntity, swapField, true, gameState);
					}

					if(addToSwapField) {
						gameState->swapField = field;
						rebaseField(field, gameState->swapFieldP);
					}
					else if(removeFromSwapField) {
						assert(gameState->swapField);
						gameState->swapField = NULL;
					}

					if (addToParent) {
						V2 newBaseP = getBottomFieldP(consoleEntity, gameState, spec);

						for (s32 fieldIndex = 0; fieldIndex < consoleEntity->numFields; fieldIndex++) {
							ConsoleField* f = consoleEntity->fields[fieldIndex];
							f->offs = v2(0, -spec->fieldSize.y - field->childYOffs); 
						}

						addField(consoleEntity, field);
						rebaseField(field, newBaseP);

						onAddConsoleFieldToEntity(consoleEntity, consoleEntity->fields[consoleEntity->numFields - 1], false, gameState);
					}
					else if (removeFromParent) {
						setFlags(field, ConsoleFlag_remove);

						if(field->type == ConsoleField_shootsAtTarget) {
							clearFlags(consoleEntity, EntityFlag_shooting|EntityFlag_unchargingAfterShooting);
						}

						bool encounteredField = false;

						for (s32 fieldIndex = 0; fieldIndex < consoleEntity->numFields; fieldIndex++) {
							ConsoleField* f = consoleEntity->fields[fieldIndex];
							if (f == field) encounteredField = true;
							else if (!encounteredField) f->offs = v2(0, spec->fieldSize.y + field->childYOffs); 
						}
					}
				}

			}

			if(gameState->swapField) clearFlags(gameState->swapField, ConsoleFlag_selected);
			clearFlags(field, ConsoleFlag_selected);
		}
	} else {
		moveToEquilibrium(field, dt);
	}

	return result;
}

void setConsoleFieldSelectedIndex(ConsoleField* field, s32 newIndex, FieldSpec* spec) {
	if (field->type == ConsoleField_unlimitedInt) {
		field->selectedIndex = newIndex;
	} 
	else {
		if (field->type == ConsoleField_bool) {
			field->selectedIndex = 1 - field->selectedIndex;
		} else {
			if (newIndex < 0) newIndex = 0;
			if (newIndex >= field->numValues) newIndex = field->numValues - 1;
			field->selectedIndex = newIndex;
		}

		assert(field->selectedIndex >= 0 && field->selectedIndex < field->numValues);
	}
	
	spec->blueEnergy -= field->tweakCost;
}

bool clickConsoleButton(R2 bounds, ConsoleField* field, Input* input, FieldSpec* spec, bool increase, ButtonState* state) {
	bool clickHandled = false;

	bool canAfford = field->numChildren || spec->blueEnergy >= field->tweakCost;

	if(canAfford) {
		ConsoleFlag flag = increase ? ConsoleFlag_wasRightArrowSelected : ConsoleFlag_wasLeftArrowSelected;
		bool32 wasClicked = isSet(field, flag);
		clearFlags(field, flag);

		if(wasClicked || pointInsideRect(bounds, input->mouseInMeters)) {
			if(input->leftMouse.pressed) {
				clickHandled = true;
				setFlags(field, flag);
				*state = ButtonState_clicked;
			} else {
				if(wasClicked) {
					clickHandled = true;

					if(field->numChildren) {
						toggleFlags(field, ConsoleFlag_childrenVisible);
					} else {
						setConsoleFieldSelectedIndex(field, field->selectedIndex - 1 + 2 * increase, spec);
					}
				}

				*state = ButtonState_hover;
			}
		} else {
			*state = ButtonState_default;
		}
	} else { 
		*state = ButtonState_cantAfford;
	}	

	return clickHandled;
}

bool drawTileArrow(V2 p, V2 offset, ConsoleField* field, RenderGroup* group, Input* input, FieldSpec* spec, Orientation orientation, bool moving) {
	V2 arrowSize;

	if(orientation == Orientation_0 || orientation == Orientation_180){
		arrowSize = spec->tileArrowSize;
	} else {
		arrowSize = v2(spec->tileArrowSize.y, spec->tileArrowSize.x);
	} 

	offset += arrowSize * 0.5;
	bool increase;

	switch(orientation) {
		case Orientation_0:
			p.x += offset.x;
			increase = true;
			break;
		case Orientation_90:
			p.y -= offset.y;
			increase = false;
			break;
		case Orientation_180:
			p.x -= offset.x;
			increase = false;
			break;
		case Orientation_270:
			p.y += offset.y;
			increase = true;
			break;

		InvalidDefaultCase;
	}

	R2 arrowBounds = rectCenterDiameter(p, arrowSize);
	Color color;
	bool clickHandled = false;

	if(moving) {
		color = createColor(255, 255, 100, 255);
	} 
	else {
		ButtonState state;
		clickHandled = clickConsoleButton(arrowBounds, field, input, spec, increase, &state);

		switch(state) {
			case ButtonState_cantAfford:
				color = createColor(255, 255, 255, 100);
				break;
			case ButtonState_default:
				color = WHITE;
				break;
			case ButtonState_hover:
				color = createColor(100, 100, 100, 255);
				break;
			case ButtonState_clicked:
				color = createColor(200, 200, 200, 255);
				break;

			InvalidDefaultCase;
		}
	}

	pushTexture(group, &spec->tileHackArrow, arrowBounds, false, DrawOrder_gui, false, orientation, color);	

	return clickHandled;
}

bool drawValueArrow(V2 p, ConsoleField* field, RenderGroup* group, Input* input, FieldSpec* spec, bool facesRight) {
	bool clickHandled = false;

	R2 triangleBounds = rectCenterDiameter(p, spec->triangleSize);
	bool drawArrow = (field->type == ConsoleField_unlimitedInt);

	if(facesRight) {
		if(field->selectedIndex < field->numValues - 1) drawArrow = true;
	} else {
		if(field->selectedIndex > 0) drawArrow = true;
	}

	ButtonState state = ButtonState_cantAfford;

	if(drawArrow) {
		R2 clickBounds = triangleBounds;
		clickBounds.max.y -= spec->fieldSize.y;

		clickConsoleButton(clickBounds, field, input, spec, facesRight, &state);
	}	

	TextureData* tex = NULL;
	Color color = WHITE;

	switch(state) {
		case ButtonState_cantAfford:
			tex = &spec->leftButtonUnavailable;
			color = createColor(255, 255, 255, 100);
			break;
		case ButtonState_default:
			tex = &spec->leftButtonDefault;
			break;
		case ButtonState_hover:
			tex = &spec->leftButtonDefault;
			color = createColor(150, 150, 150, 255);
			break;
		case ButtonState_clicked:
			tex = &spec->leftButtonClicked;
			break;

		InvalidDefaultCase;
	}

	assert(tex);
	pushTexture(group, tex, triangleBounds, facesRight, DrawOrder_gui, false, Orientation_0, color);

	return clickHandled;
}

bool drawFields(GameState* gameState, Entity* entity, double dt, V2* fieldP, FieldSpec* spec);

bool drawConsoleField(ConsoleField* field, RenderGroup* group, Input* input, FieldSpec* spec, bool drawValue, bool drawField) {
	bool result = false;

	char valueStr[200];

	if (!isSet(field, ConsoleFlag_remove)) {
		bool hasValue = true;

		switch(field->type) {
			case ConsoleField_s32: {
				sprintf(valueStr, "%d", field->s32Values[field->selectedIndex]);
			} break;

			case ConsoleField_double: {
				sprintf(valueStr, "%.1f", field->doubleValues[field->selectedIndex]);
			} break;

			case ConsoleField_unlimitedInt: {
				sprintf(valueStr, "%d", field->selectedIndex);
			} break;

			case ConsoleField_bool: {
				if (field->selectedIndex == 0) {
					sprintf(valueStr, "false");
				} else if (field->selectedIndex == 1) {
					sprintf(valueStr, "true");
				}
			} break;

			case ConsoleField_Alertness: {
				assert(field->selectedIndex >= 0 && field->selectedIndex < Alertness_count);
				Alertness value = (Alertness)field->selectedIndex;

				switch(value) {
					case Alertness_asleep: {
						sprintf(valueStr, "asleep");
					} break;
					case Alertness_patrolling: {
						sprintf(valueStr, "patrolling");
					} break;
					case Alertness_searching: {
						sprintf(valueStr, "searching");
					} break;
					case Alertness_detected: {
						sprintf(valueStr, "detected");
					} break;

					InvalidDefaultCase;
				}
			} break;

			default:
				hasValue = false;
		}

		V2 fieldP = field->p;
		if(hasValues(field)) fieldP += v2(0, spec->valueSize.y - spec->valueBackgroundPenetration);
		R2 fieldBounds = rectCenterDiameter(fieldP, spec->fieldSize);

		if (drawValue) {
			if(hasValue) {
				V2 valueP = fieldP + v2(0.1, (spec->valueSize.y + spec->fieldSize.y) * -0.5 + spec->valueBackgroundPenetration);
				R2 valueBounds = rectCenterDiameter(valueP, spec->valueSize);
				pushTexture(group, &spec->valueBackground, valueBounds, false, DrawOrder_gui);

				V2 textP = valueP - v2(0, 0.225);
				pushText(group, &spec->consoleFont, valueStr, textP, WHITE, TextAlignment_center);

				V2 leftTriangleP = v2(valueP.x - spec->valueSize.x*0.5, fieldP.y - spec->valueBackgroundPenetration * 0.5);
				V2 rightTriangleP = leftTriangleP + v2(spec->valueSize.x, 0);

				if (drawValueArrow(leftTriangleP, field, group, input, spec, false)) result = true;
				if (drawValueArrow(rightTriangleP, field, group, input, spec, true)) result = true;
			} else {
				V2 triangleP = field->p + v2((spec->triangleSize.x + spec->fieldSize.x) / 2.0f + spec->spacing.x, 0.12);

				if (field->numChildren &&
					drawTileArrow(triangleP, v2(0, 0), field, group, input, spec, Orientation_90, false)) {
					result = true;
				}
			}
		}

		if(drawField) {
			TextureData* fieldTex = hasValues(field) ? &spec->attribute : &spec->behaviour;
			pushTexture(group, fieldTex, fieldBounds, false, DrawOrder_gui);

			V2 nameP = fieldP + v2(0.14, -0.04);
			pushText(group, &spec->consoleFont, field->name, nameP, WHITE, TextAlignment_center);

			char tweakCostStr[25];
			sprintf(tweakCostStr, "%d", field->tweakCost);

			V2 costP = fieldP + v2(-1.13, -0.05);
			pushText(group, &spec->consoleFont, tweakCostStr, costP, WHITE, TextAlignment_center);
		}
	}

	return result;
}

void moveFieldChildren(ConsoleField** fields, s32 fieldsCount, FieldSpec* spec, double dt) {
	for (s32 fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];

		if (field->numChildren) {
			double fieldSpeed = 6;
			double delta = fieldSpeed * dt;
			double maxY = getTotalYOffset(field->children, field->numChildren, spec, false);
			double minY = 0;

			if (isSet(field, ConsoleFlag_childrenVisible)) {
				field->childYOffs += delta;
			} else {
				field->childYOffs -= delta;
			}

			field->childYOffs = clamp(field->childYOffs, minY, maxY);
		}
	}
}

bool calcFieldPositions(GameState* gameState, ConsoleField** fields, s32 fieldsCount, double dt, V2* fieldP, FieldSpec* spec, bool isPickupField) {
	bool result = false;

	for (s32 fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];
		double fieldHeight = getTotalFieldHeight(spec, field);
		fieldP->y -= fieldHeight;
		field->p = *fieldP + field->offs;

		//NOTE: Currently, fields with values like speed cannot be moved.
		if (!hasValues(field) && !(isPickupField && fieldIndex == fieldsCount - 1)) {
			if(moveField(field, gameState, dt, spec)) result = true;
		} else {
			moveToEquilibrium(field, dt);
		}

		field->p = *fieldP + field->offs;

		if (!isSet(field, ConsoleFlag_remove)) {
			if (field->numChildren && field->childYOffs) {

				V2 startFieldP = *fieldP;
				fieldP->x += spec->childInset;
				*fieldP += field->offs;

				calcFieldPositions(gameState, field->children, field->numChildren, dt, fieldP, spec, isPickupField);

				*fieldP = startFieldP - v2(0, field->childYOffs);
			}
		} else {
			fieldP->y -= field->childYOffs;
		}
	}

	return result;
}

bool drawFieldsRaw(RenderGroup* group, Input* input, ConsoleField** fields, s32 fieldsCount, FieldSpec* spec, bool drawFieldSprite = true) {
	bool result = false;

	for (s32 fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];
		if (drawConsoleField(field, group, input, spec, true, drawFieldSprite)) result = true;

		if (field->numChildren && field->childYOffs) {
			V2 rectSize = v2(spec->fieldSize.x, field->childYOffs);//getTotalYOffset(field->children, field->numChildren, spec, false));
			V2 clipTopRight = field->p + v2(spec->fieldSize.x, -spec->fieldSize.y) * 0.5 + v2(spec->childInset, 0);

			R2 clipRect = r2(clipTopRight, clipTopRight - rectSize);
			pushClipRect(group, clipRect);

			//pushFilledRect(group, clipRect, MAGENTA, false);
			if (drawFieldsRaw(group, input, field->children, field->numChildren, spec)) result = true;

			pushDefaultClipRect(group);
		}
	}

	return result;
}

bool drawFields(GameState* gameState, Entity* entity, s32 fieldSkip, double dt, V2* fieldP, FieldSpec* spec) {
	bool result = false;

	for(s32 fieldIndex = 0; fieldIndex < fieldSkip; fieldIndex++) {
		ConsoleField* field = entity->fields[fieldIndex];
		fieldP->y -= getTotalFieldHeight(spec, field);
	}

	if(calcFieldPositions(gameState, entity->fields + fieldSkip, entity->numFields - fieldSkip, dt, fieldP, spec, entity->type == EntityType_pickupField)) {
		result = true;
	}

	if(drawFieldsRaw(gameState->renderGroup, &gameState->input, entity->fields + fieldSkip, entity->numFields - fieldSkip, spec)) {
		result = true;
	}

	return result;
}

void drawWaypointInformation(ConsoleField* field, RenderGroup* group, FieldSpec* spec) {
	if(field->type == ConsoleField_followsWaypoints) {
		if(field->childYOffs > 0) {
			Waypoint* w = field->curWaypoint;
			assert(w);

			int alpha = (int)(field->childYOffs / getMaxChildYOffset(field, spec) * 255.0 + 0.5);

			double waypointSize = 0.125;
			double lineThickness = 0.025;
			double lineDashSize = 0.3;
			double lineSpaceSize = lineDashSize * 0.5;
			Color lineColor = createColor(105, 255, 126, alpha);
			Color currentLineColor = createColor(105, 154, 255, alpha);
			double arrowSize = 0.15;
			V2 arrowDimens = v2(1, 1) * arrowSize;

			TextureData* waypointTex = &spec->waypoint;
			TextureData* waypointArrowTex = &spec->waypointArrow;

			while(w) {
				Waypoint* next = w->next;
				assert(next);
				
				if(next == field->curWaypoint) {
					lineColor = currentLineColor;
				}

				R2 waypointBounds = rectCenterRadius(w->p, v2(1, 1) * waypointSize);

				pushTexture(group, waypointTex, waypointBounds, false, DrawOrder_gui, true, 
							Orientation_0, lineColor);

				V2 lineStart = w->p;
				V2 lineEnd = next->p;

				V2 lineDir = normalize(lineEnd - lineStart);
				V2 waypointOffset = lineDir * waypointSize;

				lineStart += 2*waypointOffset;
				lineEnd -= waypointOffset + lineDir * arrowSize;

				double rad = getRad(lineDir);
				V2 arrowCenter = lineEnd;
				R2 arrowBounds = rectCenterRadius(arrowCenter, arrowDimens);

				pushDashedLine(group, lineColor, lineStart, lineEnd, lineThickness, lineDashSize, lineSpaceSize, true);
				pushRotatedTexture(group, waypointArrowTex, arrowBounds, rad, lineColor, true);

				w = next;
				if(w == field->curWaypoint) break;
			}
		}
	}
}

bool updateConsole(GameState* gameState, double dt) {
	bool clickHandled = false;

	FieldSpec* spec = &gameState->fieldSpec;

#if 1
	{ //NOTE: This draws the gravity field
		ConsoleField* gravityField = gameState->gravityField;
		assert(gravityField);
		if (drawFieldsRaw(gameState->renderGroup, &gameState->input, &gravityField, 1, spec, false)) clickHandled = true;

		gameState->gravity = v2(0, gravityField->doubleValues[gravityField->selectedIndex]);
	}

	{ //NOTE: This draws the time field
		ConsoleField* timeField = gameState->timeField;
		assert(timeField);
		if (drawFieldsRaw(gameState->renderGroup, &gameState->input, &timeField, 1, spec, false)) clickHandled = true;
	}
#endif

		//NOTE: This draws the swap field
	if (gameState->swapField) {
		moveFieldChildren(&gameState->swapField, 1, spec, dt);

		V2 swapFieldP = gameState->swapFieldP + v2(0, spec->fieldSize.y);

		if (calcFieldPositions(gameState, &gameState->swapField, 1, dt, &swapFieldP, spec, false))
			clickHandled = true;

		if (gameState->swapField) {
			if (drawFieldsRaw(gameState->renderGroup, &gameState->input, &gameState->swapField, 1, spec))
				clickHandled = true;
		}
	}  


	Entity* entity = getEntityByRef(gameState, gameState->consoleEntityRef);

	if (entity) {
		R2 renderBounds = getRenderBounds(entity, gameState);
		
		if(entity->type != EntityType_player && !(isTileType(entity) && getMovementField(entity) == NULL)) {
			pushOutlinedRect(gameState->renderGroup, renderBounds, 0.02, RED);
		}
		
		V2 fieldP = getBottomFieldP(entity, gameState, spec);

		moveFieldChildren(entity->fields, entity->numFields, spec, dt);
		double totalYOffs = getTotalYOffset(entity->fields, entity->numFields, spec, entity->type == EntityType_pickupField);
		fieldP.y += totalYOffs;



		if (isTileType(entity)) {
			//NOTE: This draws the first two fields of the tile, xOffset and yOffset,
			//		differently
			ConsoleField* xOffsetField = entity->fields[0];
			ConsoleField* yOffsetField = entity->fields[1];

			bool showArrows = (getMovementField(entity) == NULL);

			bool moving = xOffsetField->selectedIndex != entity->tileXOffset ||
						  yOffsetField->selectedIndex != entity->tileYOffset;

			bool drawLeft = !moving || xOffsetField->selectedIndex < entity->tileXOffset;
			bool drawRight = !moving || xOffsetField->selectedIndex > entity->tileXOffset;
			bool drawTop = !moving || yOffsetField->selectedIndex < entity->tileYOffset;
			bool drawBottom = !moving || yOffsetField->selectedIndex > entity->tileYOffset;

			if (showArrows) {
				V2 clickBoxSize = getRectSize(entity->clickBox);
				V2 clickBoxCenter = getRectCenter(entity->clickBox) + entity->p - gameState->camera.p;

				R2 shieldBounds = scaleRect(renderBounds, v2(1, 1) * 1.15);
				pushTexture(gameState->renderGroup, &spec->tileHackShield, shieldBounds, false, DrawOrder_gui, false);

				V2 offset = clickBoxSize * 0.5 + spec->spacing;

				if(drawLeft && drawTileArrow(clickBoxCenter, offset, xOffsetField, gameState->renderGroup, &gameState->input, spec, Orientation_180, moving)) {
					clickHandled = true;
				}

				if(drawRight && drawTileArrow(clickBoxCenter, offset,xOffsetField, gameState->renderGroup, &gameState->input, spec, Orientation_0, moving)) {
					clickHandled = true;
				}

				if(drawTop && drawTileArrow(clickBoxCenter, offset, yOffsetField, gameState->renderGroup, &gameState->input, spec, Orientation_90, moving)) {
					clickHandled = true;
				}

				if(drawBottom && drawTileArrow(clickBoxCenter, offset, yOffsetField, gameState->renderGroup, &gameState->input, spec, Orientation_270, moving)) {
					clickHandled = true;
				}
			}								   

			fieldP.y += getTotalFieldHeight(spec, xOffsetField);
			clickHandled |= drawFields(gameState, entity, 2, dt, &fieldP, spec);
		} 


		else if (entity->type == EntityType_text) {
			ConsoleField* selectedField = entity->fields[0];

			V2 offset = v2(getRectWidth(renderBounds) * 0.5, 0);
			V2 center = getRectCenter(renderBounds);

			bool drawRight = selectedField->selectedIndex > 0;
			bool drawLeft = selectedField->selectedIndex < selectedField->numValues - 1;

			if(drawLeft && drawTileArrow(center, offset, selectedField, gameState->renderGroup, &gameState->input, spec, Orientation_0, false)) {
				clickHandled = true;
			}

			if(drawRight && drawTileArrow(center, offset, selectedField, gameState->renderGroup, &gameState->input, spec, Orientation_180, false)) {
				clickHandled = true;
			}

			clickHandled |= drawFields(gameState, entity, 1, dt, &fieldP, spec);
		}


		
		else if (entity->type == EntityType_pickupField) {
			//NOTE: The 0th field of a pickupField is ensured to be all the end of its field list
			//		so that it is rendered at the bottom. It is the actual field that would be 
			//		picked up with this entity
			ConsoleField* data = entity->fields[0];

			for(s32 fieldIndex = 0; fieldIndex < entity->numFields - 1; fieldIndex++)
				entity->fields[fieldIndex] = entity->fields[fieldIndex + 1];

			entity->fields[entity->numFields - 1] = data;

			fieldP.x += spec->fieldSize.x * 0.5 + spec->triangleSize.x + spec->spacing.x * 3;
			clickHandled |= drawFields(gameState, entity, 0, dt, &fieldP, spec);

			for(s32 fieldIndex = entity->numFields - 1; fieldIndex >= 1; fieldIndex--)
				entity->fields[fieldIndex] = entity->fields[fieldIndex - 1];

			entity->fields[0] = data;
		}

		else {
			clickHandled |= drawFields(gameState, entity, 0, dt, &fieldP, spec);
		}


		//NOTE: Draw the waypoints
		for(s32 fieldIndex = 0; fieldIndex < entity->numFields; fieldIndex++) {
			ConsoleField* field = entity->fields[fieldIndex];
			drawWaypointInformation(field, gameState->renderGroup, spec);
		}	
	}

	if(gameState->swapField) drawWaypointInformation(gameState->swapField, gameState->renderGroup, spec);

	bool32 wasConsoleEntity = getEntityByRef(gameState, gameState->consoleEntityRef) != NULL;

	//NOTE: This selects a new console entity if there isn't one and a click occurred
	Entity* player = getEntityByRef(gameState, gameState->playerRef);
	ConsoleField* playerMovementField = player ? getMovementField(player) : NULL;

	 //Don't allow hacking while the player is mid-air (doesn't apply when the player is not being keyboard controlled)
	bool playerCanHack = wasConsoleEntity || 
						 (player && isSet(player, EntityFlag_grounded)) || 
						 (!playerMovementField || playerMovementField->type != ConsoleField_keyboardControlled);
	bool newConsoleEntityRequested = !clickHandled && gameState->input.leftMouse.justPressed;

	//TODO: The spatial partition could be used to make this faster (no need to loop through every entity in the game)
	if(playerCanHack && newConsoleEntityRequested) {
		Entity* newConsoleEntity = NULL;

		for (s32 entityIndex = 0; entityIndex < gameState->numEntities; entityIndex++) {
			Entity* testEntity = gameState->entities + entityIndex;

			bool clicked = isSet(testEntity, EntityFlag_hackable) && isMouseInside(testEntity, &gameState->input);
			bool onTop = newConsoleEntity == NULL || testEntity->drawOrder > newConsoleEntity->drawOrder;
				
			if(clicked && onTop) {
				newConsoleEntity = testEntity;
				clickHandled = true;

				if(!wasConsoleEntity) {
					saveGameToArena(gameState);
				}
			}
		}

		if(newConsoleEntity) {
			gameState->consoleEntityRef = newConsoleEntity->ref;
		}
	}

	return clickHandled;
}
