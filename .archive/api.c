#include "api.h"
#include "chunk.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

// Placeholder value for invalid stack accesses. Do NOT modify it!
// static TValue noneobject = {.type = LUA_TNONE, .as = {.number = 0}};

static const TValue lua_nilvalue = makenil;

/**
 * Convert a positive or negative offset into a pointer to a particular value in
 * the VM's stack. If invalid we return the address of `noneobject` rather than
 * return `NULL` as that'll be terrible.
 *
 * If negative, we use a negative offset relative to the stack top pointer.
 * If positive, we use a positive offset relative to the stack base pointer.
 *
 * See:
 * - https://www.lua.org/source/5.1/lapi.c.html#index2adr
 */
static TValue *offset_to_address(LVM *self, int offset) {
    if (offset >= 0) {
        // Positive or zero offset in relation to base pointer, which may not
        // necessarily point to the bottom of the stack.
        return self->bp + offset;
    } else if (offset > LUA_GLOBALSINDEX) {
        // Negative offset in relation to stack pointer.
        return self->sp + offset;
    } else {
        switch (offset) {
        case LUA_GLOBALSINDEX: return &self->_G;
        default:               return NULL;
        }
    }
}

void lua_settable(LVM *self, int offset) {
    TValue *table = offset_to_address(self, offset);
    TValue *key   = offset_to_address(self, -2);
    TValue *value = offset_to_address(self, -1);
    if (!istable(table)) {
        lua_unoperror(self, offset, LUA_ERROR_INDEX);
    }
    table_set(astable(table), key, value);
    lua_pop(self, 2);
}

void lua_loadlibrary(LVM *self, const char *name, const lua_Library library) {
    // Push the desired module table onto the top of the stack at offset -2.
    // If the table didn't exist beforehand, an error would've been thrown.
    if (setjmp(self->errjmp) == 0) {
        lua_getglobal(self, name);
    } else {
        return;
    }

    for (size_t i = 0; library[i].name != NULL; i++) {
        lua_pushcfunction(self, library[i].func); // offset -1
        lua_setfield(self, -2, library[i].name);  // will pop the C function
    }
    lua_pop(self, 1); // Pop the table we were modifying
}

static int current_line(const CallFrame *cf) {
    if (cf->closure->is_c) {
        const Chunk *chunk = &cf->closure->closure.l.proto->chunk;
        int offset = (int)(cf->ip - chunk->code);
        return get_linenumber(chunk, offset);
    } else {
        return -1;
    }
}

void lua_error(LVM *self, const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "%s:%i: ", self->name, current_line(self->cf));
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\nstack traceback:\n", stderr);
    for (int i = self->fc - 1; i >= 0; i--) {
        const CallFrame *frame  = &self->frames[i];
        const TClosure *closure = frame->closure;
        fprintf(stderr, "\t%s:%i: in ", self->name, current_line(frame));
        if (closure->name == NULL) {
            fprintf(stderr, "main chunk\n");
        } else {
            fprintf(stderr, "function '%s'\n", closure->name->data);
        }
    }
    longjmp(self->errjmp, 1);
}

void lua_unoperror(LVM *self, int n, ErrType err) {
    const char *s1 = lua_typename(self, lua_type(self, n));
    switch (err) {
    case LUA_ERROR_ARITH:
        lua_error(self, "Attempt to perform arithmetic on a %s value", s1);
        break;
    case LUA_ERROR_INDEX:
        lua_error(self, "Attempt to index a %s value", s1);
        break;
    case LUA_ERROR_FIELD:
        lua_error(self, "Attempt to access field of type %s", s1);
        break;
    default:
        break;
    }
}

void lua_binoperror(LVM *self, int n1, int n2, ErrType err) {
    const char *s1 = lua_typename(self, lua_type(self, n1));
    const char *s2 = lua_typename(self, lua_type(self, n2));
    switch (err) {
    case LUA_ERROR_COMPARE:
        lua_error(self, "Attempt to compare %s with %s", s1, s2);
        break;
    case LUA_ERROR_CONCAT:
        lua_error(self, "Attempt to concatentate %s with %s", s1, s2);
        break;
    default:
        break;
    }
}

/* BASIC STACK MANIPULATION --------------------------------------------- {{{ */

int lua_gettop(LVM *self) {
    return self->sp - self->bp;
}

void lua_settop(LVM *self, int offset) {
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

void lua_dumpstack(LVM *self) {
    if (self->sp == self->bp) {
        bool isbottom = (self->sp == self->stack);
        const char *s = (isbottom) ? lua_tostring(self, 0) : "(top)";
        printf("   sp/bp -> [ %s ]\n", s);
    } else {
        printf("      sp -> [ (top) ]\n");
    }
    for (const TValue *slot = self->sp - 1; slot >= self->stack; slot--) {
        int i = self->sp - slot;
        const char *s = lua_tostring(self, -i);
        if (slot == self->bp) {
            printf("      bp -> [ %s ]\n", s);
        } else {
            printf("            [ %s ]\n", s);
        }
    }
}

/**
 * III:24.5     Function Calls
 *
 * Increments the VM's frame counter then initializes the topmost CallFrame
 * in the `frames` array using the `Function*` that was passed onto the stack
 * previously. So we set the instruction pointer to point to the first byte
 * in this particular function's bytecode, and then proceed normally as if it
 * were any other chunk. That is we go through each instruction one by one just
 * like any other in `run_bytecode()`.
 *
 * NOTE:
 *
 * Lua doesn't strictly enforce arity. So if we have too few arguments, the rest
 * are populated with `nil`. If we have too many arguments, the rest are simply
 * ignored in the function call but the stack pointer is still set properly.
 */
static bool call_luafunction(LVM *self, TClosure *luafn, int argc) {
    if (argc != luafn->arity) {
        lua_error(self, "Expected %i arguments but got %i.", luafn->arity, argc);
        return false;
    }
    // We want to iterate properly over something that has its own chunk, and as
    // a result its own lineruns. We do not do this for C functions as they do
    // not have any chunk, therefore no lineruns info, to begin with.
    luafn->chunk.prevline = -1;

    CallFrame *frame = &self->frames[self->fc++];
    frame->closure = luafn;
    frame->ip   = luafn->chunk.code;   // Beginning of function's bytecode.
    frame->bp   = self->sp - argc - 1; // Base pointer to function object itself.
    self->bp    = frame->bp;           // Allow us to use positive stack offsets.
    self->cf    = frame;               // Now point to the calling stack frame.
    return true;
}

/**
 * Calling a C function doesn't involve a lot because we don't create a stack
 * frame or anything, we simply take the arguments, run the function, and push
 * the result. Control is immediately passed back to the caller.
 */
static bool call_cfunction(LVM *self, lua_CFunction cfn, int argc) {
    const TValue res = cfn(self, argc);
    self->sp -= argc + 1; // Point to slot right below the function object.
    lua_pushobject(self, &res);
    return true;
}

bool lua_call(LVM *self, int argc) {
    if (self->fc >= LUA_MAXFRAMES) {
        lua_error(self, "Stack overflow.");
        return false;
    }
    TValue *savedbp = self->bp;
    TValue *callee = self->sp - 1 - argc;
    self->bp = self->sp - argc;
    lua_dumpstack(self);
    if (callee->type != LUA_TFUNCTION) {
        const char *tname = lua_typename(self, callee->type);
        lua_error(self, "Attempt to call %s as function", tname);
        return false;
    }

    // Call the correct function in the union based on the boolean.
    if (asfunction(callee)->is_c) {
        call_cfunction(self, ascfunction(callee), argc);
        self->bp = savedbp;
        return true;
    } else {
        return call_luafunction(self, &asluafunction(callee), argc);
    }
}

bool lua_return(LVM *self) {
    // When a function returns a value, its result will be on the top of
    // the stack. We're about to discard the function's entire stack
    // window so we hold onto the return value.
    const TValue res = *offset_to_address(self, -1);
    lua_pop(self, 1);

    // Conceptually discard the call frame. If this was the very last
    // callframe that probably indicates we've finished the top-level.
    self->fc--;
    if (self->fc == 0) {
        lua_pop(self, 1); // Pop the script itself off the VM's stack.
        return true;
    }

    // Discard all the slots the callframe was using for its parameters
    // and local variables, which are the same slots the caller (us)
    // used to push the arguments in the first place.
    self->sp = self->cf->bp;
    lua_pushobject(self, &res);

    // Return control of the stack back to the caller now that this
    // particular function call is done.
    self->cf = &self->frames[self->fc - 1];

    // Set our base pointer as well so we can access local variables
    // using 0 and positive offsets, and ensure our VM's calling frame
    // pointer is correct.
    self->bp = self->cf->bp;
    return false;
}

/* }}} ---------------------------------------------------------------------- */

/* 'GET' and 'SET' FUNCTIONS -------------------------------------------- {{{ */

void lua_getfield(LVM *self, int offset, const char *field) {
    TValue *table = offset_to_address(self, offset);
    TValue key = makestring(copy_string(self, field, strlen(field)));
    TValue value;
    if (!istable(table)) {
        lua_unoperror(self, offset, LUA_ERROR_INDEX);
    } 
    if (!table_get(astable(table), &key, &value)) {
        const char *scope = (offset == LUA_GLOBALSINDEX) ? "variable" : "field";
        lua_error(self, "Undefined %s '%s'.", scope, ascstring(&key));
    }
    lua_pushobject(self, &value);
}

void lua_setfield(LVM *self, int offset, const char *field) {
    TValue *table = offset_to_address(self, offset);
    TValue key    = makestring(copy_string(self, field, strlen(field)));
    if (!istable(table)) {
        lua_unoperror(self, offset, LUA_ERROR_INDEX);
    } else if (!isstring(&key)) {
        lua_unoperror(self, offset, LUA_ERROR_FIELD);
    }
    table_set(astable(table), &key, offset_to_address(self, - 1));
    lua_pop(self, 1);
}

/* }}} ---------------------------------------------------------------------- */

VType lua_type(LVM *self, int offset) {
    return offset_to_address(self, offset)->type;
}

const char *lua_typename(LVM *self, VType type) {
    unused(self);
    return get_tnameinfo(type)->what;
}

/* 'IS' FUNCTIONS ------------------------------------------------------- {{{ */

bool lua_iscfunction(LVM *self, int offset) {
    const TValue *v = offset_to_address(self, offset);
    return iscfunction(v);
}

/* }}} */

bool lua_equal(LVM *self, int offset1, int offset2) {
    const TValue *lhs = offset_to_address(self, offset1);
    const TValue *rhs = offset_to_address(self, offset2);
    if (lhs->type != rhs->type) {
        return false;
    }
    switch (lhs->type) {
    case LUA_TBOOLEAN:  return lhs->as.boolean == rhs->as.boolean;
    case LUA_TNIL:      return true; // nil == nil, always.
    case LUA_TNUMBER:   return lhs->as.number == rhs->as.number;
    case LUA_TTABLE:    // All objects are interned so pointer comparisons work.
    case LUA_TFUNCTION:
    case LUA_TSTRING:   return lhs->as.object == rhs->as.object;
    default:            return false; // LUA_TNONE and LUA_TCOUNT
    }
}

/* 'AS' FUNCTIONS ------------------------------------------------------- {{{ */

bool lua_asboolean(LVM *self, int offset) {
    const TValue *v = offset_to_address(self, offset);
    if (isnil(v)) {
        return false;
    } else if (isboolean(v)) {
        return asboolean(v);
    }
    // Every other value, even 0 and empty containers, is considered true.
    return true;
}

lua_Number lua_asnumber(LVM *self, int offset) {
    const TValue *v = offset_to_address(self, offset);
    return isnumber(v) ? asnumber(v) : (lua_Number)0;
}

TString *lua_aststring(LVM *self, int offset) {
    TValue *v = offset_to_address(self, offset);
    return (isstring(v)) ? asstring(v) : NULL;
}

Proto *lua_asfunction(LVM *self, int offset) {
    TValue *v = offset_to_address(self, offset);
    return (isfunction(v)) ? asfunction(v) : NULL;
}

Table *lua_astable(LVM *self, int offset) {
    TValue *v = offset_to_address(self, offset);
    return (istable(v)) ? astable(v) : NULL;
}

/* }}} --------------------------------------------------------------------- */

lua_Number lua_tonumber(LVM *self, int offset) {
    TValue *v = offset_to_address(self, offset);
    if (isnumber(v)) {
        return asnumber(v);
    } else if (isstring(v)) {
        lua_Number result;
        return check_tonumber(ascstring(v), &result) ? result : 0;
    } else {
        return 0;
    }
}

const char *lua_tostring(LVM *self, int offset) {
    TValue *v = offset_to_address(self, offset);
    char data[LUA_MAXNUM2STR];
    const char *literal; // Will be set by `check_tostring()`.
    int len = check_tostring(v, data, &literal);
    if (len == 0) {
        return literal;
    }
    TString *res = copy_string(self, data, len);
    return res->data;
}

/* PUSH FUNCTIONS ------------------------------------------------------- {{{ */

void lua_pushboolean(LVM *self, bool b) {
    TValue v = makeboolean(b);
    lua_pushobject(self, &v);
}

void lua_pushnil(LVM *self) {
    lua_pushobject(self, &lua_nilvalue);
}

void lua_pushnumber(LVM *self, lua_Number n) {
    TValue v = makenumber(n);
    lua_pushobject(self, &v);
}

void lua_pushlstring(LVM *self, const char *data, size_t len) {
    TValue v = makestring(copy_string(self, data, len));
    lua_pushobject(self, &v);
}

void lua_pushstring(LVM *self, const char *data) {
    if (data == NULL) {
        lua_pushnil(self);
    } else {
        lua_pushlstring(self, data, strlen(data));
    }
}

void lua_pushtable(LVM *self, Table *table) {
    TValue v = maketable(table);
    lua_pushobject(self, &v);
}

void lua_pushfunction(LVM *self, Proto *tfunc) {
    TValue v = makefunction(tfunc);
    lua_pushobject(self, &v);
}

void lua_pushcfunction(LVM *self, lua_CFunction function) {
    Proto *tfunc = new_cfunction(self, function);
    lua_pushfunction(self, tfunc);
}

/* }}} */

void lua_concat(LVM *self) {
    TString *lhs = lua_aststring(self, -2);
    TString *rhs = lua_aststring(self, -1);
    if (lhs == NULL || rhs == NULL) {
        lua_binoperror(self, -2, -1, LUA_ERROR_CONCAT);
    }
    lua_pop(self, 2); // Clean up operands
    TString *s = concat_string(self, lhs, rhs);
    TValue o   = makestring(s);
    lua_pushobject(self, &o);
}
