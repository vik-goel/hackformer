enum ConsoleFieldType {
	ConsoleField_keyboardControlled,
	ConsoleField_movesBackAndForth,
	ConsoleField_seeksTarget,
	ConsoleField_isShootTarget,
	ConsoleField_shootsAtTarget,
	ConsoleField_double,
	ConsoleField_unlimitedInt,
	ConsoleField_int,
	ConsoleField_bool,
};

enum ConsoleFieldFlags {
	ConsoleFlag_selected = 1 << 0,
	ConsoleFlag_remove = 1 << 1,
	ConsoleFlag_childrenVisible = 1 << 2,
	ConsoleFlag_fixOffset = 1 << 3,
};

struct ConsoleField {
	ConsoleFieldType type;
	uint flags;

	char name[100];
	
	//NOTE: These attributes are used for fields with many values
	union {
		double doubleValues[5];
		int intValues[8];
	};
	int numValues;
	
	int selectedIndex;
	int initialIndex;

	int tweakCost;
	
	char valueStr[100];

	//NOTE: These attributes are used for drag and droppable fields
	V2 offs;
	V2 tempCenter;

	//NOTE: This is used for storage in the free list
	ConsoleField* next;

	//NOTE: This is used for hierarchal console fields
	ConsoleField* children[4];
	int numChildren;
	double childYOffs;
};

bool isConsoleFieldMovementType(ConsoleField* field) {
	bool result = field->type == ConsoleField_keyboardControlled ||
				  field->type == ConsoleField_movesBackAndForth ||
				  field->type == ConsoleField_seeksTarget;
	return result;
}

//TODO: This could be a flag instead
bool canOnlyHaveOneFieldOfType(ConsoleFieldType type) {
	bool result = type == ConsoleField_isShootTarget ||
				  type == ConsoleField_shootsAtTarget;

 	return result;
}

void setFlags(ConsoleField* field, uint flags) {
	field->flags |= flags;
} 

void clearFlags(ConsoleField* field, uint flags) {
	field->flags &= ~flags;
} 

void toggleFlags(ConsoleField* field, uint toggle) {
	field->flags ^= toggle;
}

bool isSet(ConsoleField* field, uint flags) {
	bool result = (field->flags & flags) != 0;
	return result;
}