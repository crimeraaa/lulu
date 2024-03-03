#include "api.h"
#include "vm.h"

void lua_settop(lua_VM *self, ptrdiff_t offset) {
    if (offset >= 0) {
        // Get positive offset in relation to base pointer.
        // Fill gaps with nils.
        while (self->sp < self->bp + offset) {
            *self->sp = makenil;
            self->sp++;
        }
        self->sp = self->bp + offset;
    } else {
        // Is negative offset in relation to stack top pointer.
        self->sp += offset + 1; 
    }
}

bool lua_istype(const lua_VM *self, ptrdiff_t offset, ValueType type) {
    const TValue *value = lua_ptroffset(self, offset);
    return value->type == type;
}
