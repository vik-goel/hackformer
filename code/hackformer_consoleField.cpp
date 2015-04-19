void freeConsoleField(ConsoleField* field, GameState* gameState) {
	// if (field->next) {
	// 	freeConsoleField(field->next, gameState);
	// 	field->next = NULL;
	// }

	//NOTE: Since the next pointer is only used in the free list and consoles in the free list should
	//		never be freed again, the next pointer should be null. 
	assert(!field->next);

	for (int childIndex = 0; childIndex < field->numChildren; childIndex++) {
		freeConsoleField(field->children[childIndex], gameState);
		field->children[childIndex] = NULL;
	}

	field->next = gameState->consoleFreeList;
	gameState->consoleFreeList = field;
}

ConsoleField* createConsoleField(GameState* gameState, char* name, ConsoleFieldType type) {
	ConsoleField* result = NULL;

	if (gameState->consoleFreeList) {
		result = gameState->consoleFreeList;
		gameState->consoleFreeList = gameState->consoleFreeList->next;
	} else {
		result = (ConsoleField*)pushStruct(&gameState->levelStorage, ConsoleField);
	}

	*result = {};
	result->type = type;
	strcpy_s(result->name, name);

	return result;
}

ConsoleField* createUnlimitedIntField(GameState* gameState, char* name, int value, int tweakCost) {
	ConsoleField* result = createConsoleField(gameState, name, ConsoleField_unlimitedInt);

	result->selectedIndex = result->initialIndex = value;
	result->tweakCost = tweakCost;

	return result;
}

ConsoleField* createBoolField(GameState* gameState, char* name, bool value, int tweakCost) {
	ConsoleField* result = createConsoleField(gameState, name, ConsoleField_bool);

	result->initialIndex = result->selectedIndex = value ? 1 : 0;
	result->numValues = 2;
	result->tweakCost = tweakCost;

	return result;
}

#define createPrimitiveField(type, gameState, name, values, numValues, initialIndex, tweakCost) createPrimitiveField_(gameState, name, values, sizeof(type), numValues, initialIndex, tweakCost, ConsoleField_##type)
ConsoleField* createPrimitiveField_(GameState* gameState, char* name, void* values, int elemSize,
				int numValues, int initialIndex, int tweakCost, ConsoleFieldType type) {
	assert(numValues > 0);
	assert(initialIndex >= 0 && initialIndex < numValues);

	ConsoleField* result = createConsoleField(gameState, name, type);

	for (int valueIndex = 0; valueIndex < numValues; valueIndex++) {
		char* src = (char*)values + elemSize * valueIndex;
		char* dst = (char*)result->doubleValues + elemSize * valueIndex;

		for(int byteIndex = 0; byteIndex < elemSize; byteIndex++) {
			*dst++ = *src++;
		}
	}

	result->selectedIndex = result->initialIndex = initialIndex;
	result->numValues = numValues;
	result->tweakCost = tweakCost;

	return result;
}

Entity* getEntityByRef(GameState*, int ref);
ConsoleField* getMovementField(Entity* entity);
bool isMouseInside(Entity* entity, Input* input);

int getNumVisibleFields(ConsoleField** fields, int fieldsCount) {
	int result = fieldsCount;

	for (int fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];

		if (field->numChildren && isSet(field, ConsoleFlag_childrenVisible)) {
			result += getNumVisibleFields(field->children, field->numChildren);
		}
	}

	return result;
}

double getTotalYOffset(ConsoleField** fields, int fieldsCount, double fieldHeight) {
	double result = fieldsCount * fieldHeight;

	for (int fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];
		result += field->childYOffs;
	}

	return result; 
}

void moveToEquilibrium(ConsoleField* field, double dt) {
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

bool moveField(ConsoleField* field, GameState* gameState, R2 bounds, double dt, double fieldHeight) {
	bool result = false;

	if (gameState->input.leftMouseJustPressed) {
		if (pointInsideRect(bounds, gameState->input.mouseInMeters)) {
			setFlags(field, ConsoleFlag_selected);
			result = true;
		}
	}

	if (isSet(field, ConsoleFlag_selected)) {
		if (gameState->input.leftMousePressed) {
			field->offs += gameState->input.dMouseMeters;
		} else {
			V2 center = getRectCenter(bounds) + field->offs;
			field->tempCenter = center;

			Entity* consoleEntity = getEntityByRef(gameState, gameState->consoleEntityRef);
			assert(consoleEntity);

			double dstSqToConsoleEntity = dstSq(consoleEntity->p - gameState->cameraP, center);
			double dstSqToSwapField = dstSq(gameState->swapFieldP, center);

			bool addToParent = false;
			bool removeFromParent = false;

			if (gameState->swapField == field) {
				if (dstSqToConsoleEntity < dstSqToSwapField) {
					gameState->swapField = NULL;
					addToParent = true;
				}
			} else {
				if (dstSqToSwapField < dstSqToConsoleEntity) {
					if (gameState->swapField == NULL) {
						gameState->swapField = field;
						removeFromParent = true;
						//field->offs = center - gameState->swapFieldP;
					}
					else if (isConsoleFieldMovementType(gameState->swapField) && 
							 isConsoleFieldMovementType(field)) {
						ConsoleField tempField = *gameState->swapField;
						*gameState->swapField = *field;
						*field = tempField;
						field->tempCenter = gameState->swapFieldP;
					}
				}
			}

			assert(!(addToParent && removeFromParent));

			if (isConsoleFieldMovementType(field)) {
				if (addToParent) {
					ConsoleField* movementField = getMovementField(consoleEntity);

					if (movementField) {
						gameState->swapField = movementField;
						setFlags(movementField, ConsoleFlag_remove);
					}
				}
			}
			else if (field->type == ConsoleField_isShootTarget) {
					if (removeFromParent) gameState->shootTargetRef = 0;
					else if (addToParent) gameState->shootTargetRef = gameState->consoleEntityRef;
			}

			if (addToParent) {
				bool shouldAddField = true;

				if (canOnlyHaveOneFieldOfType(field->type)) {
					for (int fieldIndex = 0; fieldIndex < consoleEntity->numFields; fieldIndex++) {
						ConsoleField* testField = consoleEntity->fields[fieldIndex];

						if (testField->type == field->type) {
							shouldAddField = false;
							break;
						}
					}
				}

				if (shouldAddField) {
					for (int fieldIndex = 0; fieldIndex < consoleEntity->numFields; fieldIndex++) {
						ConsoleField* f = consoleEntity->fields[fieldIndex];
						f->offs = v2(0, -fieldHeight - field->childYOffs); 
					}

					consoleEntity->fields[consoleEntity->numFields++] = field;
					assert(consoleEntity->numFields < arrayCount(consoleEntity->fields));
				} else {
					assert(!gameState->swapField);
					gameState->swapField = field;
				}
			}
			else if (removeFromParent) {
				setFlags(field, ConsoleFlag_remove);

				bool encounteredField = false;

				for (int fieldIndex = 0; fieldIndex < consoleEntity->numFields; fieldIndex++) {
					ConsoleField* f = consoleEntity->fields[fieldIndex];
					if (f == field) encounteredField = true;
					else if (!encounteredField) f->offs = v2(0, fieldHeight + field->childYOffs); 
				}
			}

			clearFlags(field, ConsoleFlag_selected);

			//NOTE: This very large value is here to stop the field from showing up for one frame
			//		after this since the ConsoleFlag_fixOffset is set, this will be changed
			field->offs = v2(999999999, 99999999);
			setFlags(field, ConsoleFlag_fixOffset);
		}
	} else {
		moveToEquilibrium(field, dt);
	}

	return result;
}

void drawOutlinedConsoleBoxRaw(GameState* gameState, R2 bounds, double stroke, Color color) {
	pushFilledRect(gameState->renderGroup, bounds, color);

	//setColor(gameState->basicShader, 0, 0, 0, 1);

	pushOutlinedRect(gameState->renderGroup, bounds, stroke, createColor(0, 0, 0, 255));
}

bool drawOutlinedConsoleBox(ConsoleField* field, GameState* gameState, double dt, double fieldHeight,
							R2 bounds, double stroke, bool nameBox, bool hasValue = true) {
	bool result = false;

	//Texture* text = NULL;

	char* text = NULL;
	Color color = {};

	if (nameBox) {
		if (hasValue) {
			color = createColor(255, 100, 100, 255);
			//setColor(gameState->basicShader, 1, 0.4, 0.4, 1);
			moveToEquilibrium(field, dt);
		}
		else {
			color = createColor(100, 100, 255, 255);
			//setColor(gameState->basicShader, 0.4, 0.4, 1, 1);
			result = moveField(field, gameState, bounds, dt, fieldHeight);
		}

		//text = &field->nameTex;
		text = field->name;
	} else {
		color = createColor(100, 255, 100, 255);
		//setColor(gameState->basicShader, 0.4, 1, 0.4, 1);
		//text = &field->valueTex;
		text = field->valueStr;
	}

	bounds = translateRect(bounds, field->offs);
	drawOutlinedConsoleBoxRaw(gameState, bounds, stroke, color);

	double strWidth = getTextWidth(&gameState->consoleFont, gameState, text);
	V2 textP = bounds.min + v2((getRectWidth(bounds) - strWidth) / 2, 
							   (getRectHeight(bounds) - gameState->consoleFont.lineHeight) / 4);

	pushText(gameState->renderGroup, &gameState->consoleFont, text, textP);

	return result;
}

void setConsoleFieldSelectedIndex(ConsoleField* field, int newIndex, GameState* gameState) {
	if (field->type == ConsoleField_unlimitedInt) {
		field->selectedIndex = newIndex;
	} else {
		if (field->type == ConsoleField_bool) {
			field->selectedIndex = 1 - field->selectedIndex;
		} else {
			if (newIndex < 0) newIndex = 0;
			if (newIndex >= field->numValues) newIndex = field->numValues - 1;
			field->selectedIndex = newIndex;
		}

		
		assert(field->selectedIndex >= 0 && field->selectedIndex < field->numValues);
	}

	gameState->blueEnergy -= field->tweakCost;
}

bool drawConsoleTriangle(GameState* gameState, V2 triangleP, V2 triangleSize, 
						 bool facesRight, bool facesDown, bool facesUp, bool yellow, ConsoleField* field, int tweakCost) {
	bool result = false;

	R2 triangleBounds = rectCenterDiameter(triangleP, triangleSize);

	Texture* triangleTex = &gameState->consoleTriangleGrey;
	//Rotation angle = Degree0;
	//double angle = 0;
	Orientation orientation = Orientation_0;

	if (facesDown) orientation = Orientation_90;//angle = 90;//Degree90;
	if (facesUp) orientation = Orientation_270;//angle = 270;//Degree270;

	bool mouseOverTriangle = dstSq(gameState->input.mouseInMeters, triangleP) < 
							 square(min(triangleSize.x, triangleSize.y) / 2.0f);

	bool enoughEnergyToHack = !yellow && gameState->blueEnergy >= tweakCost;

	if (mouseOverTriangle) {
		if (enoughEnergyToHack) {
			triangleTex = &gameState->consoleTriangleSelected;
		}

		if (gameState->input.leftMouseJustPressed) {
			result = true;

			if (field && enoughEnergyToHack) {
				if (facesDown && tweakCost == 0) {
					toggleFlags(field, ConsoleFlag_childrenVisible);
				} else {
					int dSelectedIndex = facesRight ? -1 : 1;
					setConsoleFieldSelectedIndex(field, field->selectedIndex + dSelectedIndex, gameState);
				}
			}
		}
	} else if (enoughEnergyToHack) {
		triangleTex = &gameState->consoleTriangle;
	}

	if (yellow) triangleTex = &gameState->consoleTriangleYellow;

	pushTexture(gameState->renderGroup, triangleTex, triangleBounds, facesRight, DrawOrder_gui, false, orientation);

	//NOTE: This draws the cost of tweaking if it is not default (0 or 1)
	if (tweakCost > 1) {
		char tweakCostStr[25];
		sprintf_s(tweakCostStr, "%d", tweakCost);
		double costStrWidth = getTextWidth(&gameState->consoleFont, gameState, tweakCostStr);
		V2 costP = triangleBounds.min;

		if (facesDown) {
			costP += v2(triangleSize.x / 2 - costStrWidth / 2, gameState->consoleFont.lineHeight);
		}
		else if (facesUp) {
			costP += v2(triangleSize.x / 2 - costStrWidth / 2, gameState->consoleFont.lineHeight / 4);
		}
		else if (facesRight) {
			costP += v2(triangleSize.x / 2 + costStrWidth / 4, gameState->consoleFont.lineHeight / 2);
		}
		else {
			costP += v2(costStrWidth / 2, gameState->consoleFont.lineHeight / 2);
		}

		pushText(gameState->renderGroup, &gameState->consoleFont, tweakCostStr, costP);
	}

	return result;
}

bool drawFields(GameState* gameState, double dt, ConsoleField** fields, int fieldsCount, V2* fieldP,
				V2 fieldSize, V2 triangleSize, V2 valueSize, double spacing, double stroke);

bool drawConsoleField(ConsoleField* field, GameState* gameState, double dt, V2* fieldP, 
					  V2 fieldSize, V2 triangleSize, V2 valueSize, double spacing, double stroke) {
	if (isSet(field, ConsoleFlag_fixOffset)) {
		field->offs = field->tempCenter - *fieldP;
		clearFlags(field, ConsoleFlag_fixOffset);
	}

	bool result = false;

	if (!isSet(field, ConsoleFlag_remove)) {
		bool hasValue = true;

		switch(field->type) {
			case ConsoleField_int: {
				sprintf_s(field->valueStr, "%d", field->intValues[field->selectedIndex]);
			} break;

			case ConsoleField_double: {
				sprintf_s(field->valueStr, "%.1f", field->doubleValues[field->selectedIndex]);
			} break;

			case ConsoleField_unlimitedInt: {
				sprintf_s(field->valueStr, "%d", field->selectedIndex);
			} break;

			case ConsoleField_bool: {
				if (field->selectedIndex == 0) {
					sprintf_s(field->valueStr, "false");
				} else if (field->selectedIndex == 1) {
					sprintf_s(field->valueStr, "true");
				}
			} break;

			default:
				hasValue = false;
		}

		R2 fieldBounds = rectCenterDiameter(*fieldP, fieldSize);

		if (drawOutlinedConsoleBox(field, gameState, dt, fieldSize.y, fieldBounds, stroke, true, hasValue)) {
			result = true;
		}

		if (hasValue) {
			bool alwaysDrawTriangles = (field->type == ConsoleField_unlimitedInt);

			V2 triangleP = *fieldP + v2((fieldSize.x + triangleSize.x) / 2 + spacing, 0);
		
			if ((field->selectedIndex != 0 || alwaysDrawTriangles) && 
				drawConsoleTriangle(gameState, triangleP + field->offs, triangleSize, 
					true, false, false, false, field, field->tweakCost)) {
				result = true;
			}

			V2 valueP = triangleP + v2((triangleSize.x + valueSize.x) / 2.0f + spacing, 0);
			R2 valueBounds = rectCenterDiameter(valueP, valueSize);

			drawOutlinedConsoleBox(field, gameState, dt, fieldSize.y, valueBounds, stroke, false);

			triangleP = valueP + v2((triangleSize.x + valueSize.x) / 2.0f + spacing, 0);

			if ((field->selectedIndex != field->numValues - 1 || alwaysDrawTriangles) &&
				drawConsoleTriangle(gameState, triangleP + field->offs, triangleSize,
					 false, false, false, false, field, field->tweakCost)) {
				result = true;
			}
		} else {
			V2 triangleP = *fieldP + v2((triangleSize.x + fieldSize.x) / 2.0f + spacing, 0) + field->offs;

			if (field->numChildren &&
				drawConsoleTriangle(gameState, triangleP, triangleSize, false, true, false, false, field, field->tweakCost)) {
				result = true;
			}
		}

		if (field->numChildren && field->childYOffs) {
			double inset = fieldSize.x / 4.0f;

			V2 startFieldP = *fieldP;

			fieldP->x += inset;
			*fieldP += field->offs;

			//NOTE: 0.1 and 2.5 are just arbitrary values which make the clipRect big enough
			V2 clipPoint1 = *fieldP - v2(fieldSize.x / 2 + 0.1, field->childYOffs + fieldSize.y / 2 + spacing);
			V2 clipPoint2 = *fieldP + v2(fieldSize.x / 2 + 2.5, -fieldSize.y / 2);

			if (field == gameState->swapField) {
				clipPoint1.y -= 0;
			}

			R2 clipRect = r2(clipPoint1, clipPoint2);
			pushClipRect(gameState->renderGroup, clipRect);

			//setColor(gameState->renderer, 255, 0, 0, 255);
			//drawFilledRect(gameState, clipRect, -gameState->cameraP);

			result |= drawFields(gameState, dt, field->children, field->numChildren, fieldP,
					  			 fieldSize, triangleSize, valueSize, spacing, stroke);

			*fieldP = startFieldP - v2(0, field->childYOffs);
			pushDefaultClipRect(gameState->renderGroup);
		}

	}

	return result;
}

void moveFieldChildren(ConsoleField** fields, int fieldsCount, double fieldHeight, double dt) {
	for (int fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		ConsoleField* field = fields[fieldIndex];

		if (field->numChildren) {
			double fieldSpeed = 4;
			double delta = fieldSpeed * dt;
			double maxY = field->numChildren * fieldHeight;
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

bool drawFields(GameState* gameState, double dt, ConsoleField** fields, int fieldsCount, V2* fieldP,
				V2 fieldSize, V2 triangleSize, V2 valueSize, double spacing, double stroke) {
	bool result = false;

	for (int fieldIndex = 0; fieldIndex < fieldsCount; fieldIndex++) {
		fieldP->y -= fieldSize.y + spacing;
		ConsoleField* field = fields[fieldIndex];
		result |= drawConsoleField(field, gameState, dt, fieldP, 
								   fieldSize, triangleSize, valueSize, spacing, stroke);	
	}

	return result;
}

void updateConsole(GameState* gameState, double dt) {
	bool clickHandled = false;

	V2 fieldSize = v2(2.1f, 0.4f);
	V2 triangleSize = v2(fieldSize.y, fieldSize.y);
	V2 valueSize = v2(fieldSize.x * 0.5f, fieldSize.y);
	double spacing = 0.05f;
	double stroke = 0.02f;

	Entity* entity = getEntityByRef(gameState, gameState->consoleEntityRef);

	if (entity) {
		R2 renderBounds = translateRect(entity->clickBox, entity->p - gameState->cameraP);
		//setColor(gameState->basicShader, 1, 0, 0, 1);
		pushOutlinedRect(gameState->renderGroup, renderBounds, 0.02, createColor(255, 0, 0, 255));

		V2 fieldOffset;

		V2 entityScreenP = (entity->p - gameState->cameraP) * gameState->pixelsPerMeter;
		bool onScreenRight = entityScreenP.x >= gameState->windowWidth / 2;

		if (onScreenRight) {
			fieldOffset = v2(fieldSize.x * -0.75, 0);
		} else {
			fieldOffset = v2(fieldSize.x * 0.75, 0);
		}

		V2 fieldP = getRectCenter(renderBounds) + fieldOffset;

		//int numFields = getNumVisibleFields(entity->fields, entity->numFields);
		//fieldP.y += (fieldSize.y + spacing) * numFields;

		moveFieldChildren(entity->fields, entity->numFields, fieldSize.y + spacing, dt);
		double totalYOffs = getTotalYOffset(entity->fields, entity->numFields, fieldSize.y + spacing);
		fieldP.y += totalYOffs;



		if (entity->type == EntityType_tile || entity->type == EntityType_heavyTile) {
			//NOTE: This draws the first two fields of the tile, xOffset and yOffset,
			//		differently
			ConsoleField* xOffsetField = entity->fields[0];
			ConsoleField* yOffsetField = entity->fields[1];

			bool showArrows = getMovementField(entity) == NULL;

			bool yellow = xOffsetField->selectedIndex != entity->tileXOffset ||
						  yOffsetField->selectedIndex != entity->tileYOffset;

			bool drawLeft = !yellow || xOffsetField->selectedIndex < entity->tileXOffset;
			bool drawRight = !yellow || xOffsetField->selectedIndex > entity->tileXOffset;
			bool drawTop = !yellow || yOffsetField->selectedIndex < entity->tileYOffset;
			bool drawBottom = !yellow || yOffsetField->selectedIndex > entity->tileYOffset;

			if (showArrows) {
				V2 clickBoxSize = getRectSize(entity->clickBox);
				V2 clickBoxCenter = getRectCenter(entity->clickBox);

				V2 halfTriangleOffset = v2((clickBoxSize.x + triangleSize.x) / 2.0 + spacing, 
										   (clickBoxSize.y + triangleSize.y) / 2.0 + spacing);

				V2 triangleP = entity->p + clickBoxCenter - gameState->cameraP - v2(halfTriangleOffset.x, 0);

				if (drawLeft && drawConsoleTriangle(gameState, triangleP, triangleSize, 
									true, false, false, yellow, xOffsetField, xOffsetField->tweakCost)) {
					clickHandled = true;
				}

				triangleP += v2(halfTriangleOffset.x * 2, 0);

				if (drawRight && drawConsoleTriangle(gameState, triangleP, triangleSize, 
									false, false, false, yellow, xOffsetField, xOffsetField->tweakCost)) {
					clickHandled = true;
				}

				triangleP = entity->p + clickBoxCenter - gameState->cameraP - v2(0, halfTriangleOffset.y);

				if (drawTop && drawConsoleTriangle(gameState, triangleP, triangleSize, 
								true, false, true, yellow, yOffsetField, yOffsetField->tweakCost)) {
					clickHandled = true;
				}

				triangleP += v2(0, halfTriangleOffset.y * 2);

				if (drawBottom && drawConsoleTriangle(gameState, triangleP, triangleSize, 
								false, false, true, yellow, yOffsetField, yOffsetField->tweakCost)) {
					clickHandled = true;
				}
			}								   

			clickHandled |= drawFields(gameState, dt, entity->fields + 2, entity->numFields - 2, &fieldP,
									   fieldSize, triangleSize, valueSize, spacing, stroke);
		} 



		else if (entity->type == EntityType_text) {
			ConsoleField* selectedField = entity->fields[0];

			double widthOffs = triangleSize.x * 0.6;
			double heightOffs = getRectHeight(renderBounds) / 2;
			V2 rightTriangleP = renderBounds.max + v2(widthOffs, -heightOffs);

			bool drawRight = selectedField->selectedIndex > 0;
			bool drawLeft = selectedField->selectedIndex < selectedField->numValues - 1;

			if (drawLeft && drawConsoleTriangle(gameState, rightTriangleP, triangleSize, 
									false, false, false, false, selectedField, selectedField->tweakCost)) {
					clickHandled = true;
			}

			V2 leftTriangleP = renderBounds.min + v2(-widthOffs, heightOffs);

			if (drawRight && drawConsoleTriangle(gameState, leftTriangleP, triangleSize, 
									true, false, false, false, selectedField, selectedField->tweakCost)) {
					clickHandled = true;
			}

			clickHandled |= drawFields(gameState, dt, entity->fields + 1, entity->numFields - 1, &fieldP,
							   fieldSize, triangleSize, valueSize, spacing, stroke);
		}



		else {
			clickHandled |= drawFields(gameState, dt, entity->fields, entity->numFields, &fieldP,
				   				   	   fieldSize, triangleSize, valueSize, spacing, stroke);
		}
	}




	//NOTE: This draws the swap field
	if (gameState->consoleEntityRef) {
		if (!gameState->swapField || lengthSq(gameState->swapField->offs) > 0) {
			//setColor(gameState->basicShader, 1, 1, 0.4, 1);
			drawOutlinedConsoleBoxRaw(gameState, rectCenterDiameter(gameState->swapFieldP, fieldSize),
									 stroke, createColor(255, 255, 100, 255));
		}

		if (gameState->swapField) {
			moveFieldChildren(&gameState->swapField, 1, fieldSize.y + spacing, dt);
			clickHandled |= drawConsoleField(gameState->swapField, gameState, dt, &gameState->swapFieldP, 
										 	 fieldSize, triangleSize, valueSize, spacing, stroke);	
		}  
	}

	bool wasConsoleEntity = getEntityByRef(gameState, gameState->consoleEntityRef) != NULL;

	//NOTE: This deselects the console entity if somewhere else is clicked
	if (!clickHandled && gameState->input.leftMouseJustPressed) {
		gameState->consoleEntityRef = 0;
	}


	//NOTE: This selects a new console entity if there isn't one and a click occurred
	Entity* player = getEntityByRef(gameState, gameState->playerRef);

	//Don't allow hacking while the player is mid-air
	if(wasConsoleEntity || (player && isSet(player, EntityFlag_grounded))) {
		if (!getEntityByRef(gameState, gameState->consoleEntityRef)) {
			if (!clickHandled && gameState->input.leftMouseJustPressed) {
				for (int entityIndex = 0; entityIndex < gameState->numEntities; entityIndex++) {
					Entity* entity = gameState->entities + entityIndex;
						
					if(isSet(entity, EntityFlag_hackable)) {
						if (isMouseInside(entity, &gameState->input)) {
							gameState->consoleEntityRef = entity->ref;
							clickHandled = true;
							break;
						}
					}
				}
			}
		}
	}
}