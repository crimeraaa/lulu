#include "opcodes.h"
#include "object.h"

const char *const LUA_TYPENAMES[NUM_TYPETAGS] = {
    [LUA_TNIL]      = "nil",
    [LUA_TBOOLEAN]  = "boolean",
    [LUA_TNUMBER]   = "number",
    [LUA_TSTRING]   = "string",
    [LUA_TTABLE]    = "table",
    [LUA_TFUNCTION] = "function",
};

void print_value(const TValue *value) {
    switch (value->tag) {
    case LUA_TNIL:  
        printf("nil"); 
        break;
    case LUA_TBOOLEAN: 
        printf(asboolean(value) ? "true" : "false"); 
        break;
    case LUA_TNUMBER:  
        printf(LUA_NUMBER_FMT, asnumber(value)); 
        break;
    default:
        printf("%s: %p", astypename(value), (const void*)asobject(value));
        break;
    }
}
