#include "api.h"
#include "memory.h"
#include "vm.h"

// Placeholder value for invalid stack accesses. Do NOT modify it!
static TValue noneobject = {.type = LUA_TNONE, .as = {.number = 0}};
static TValue nilobject  = makenil;

/* VM CALL FRAME MANIPULATION ------------------------------------------- {{{ */

/**
 * III:23.1     If Statements
 *
 * Read the next 2 instructions and combine them into a 16-bit operand.
 *
 * The compiler emitted the 2 byte operands for a jump instruction in order of
 * msb, lsb. So our instruction pointer points at msb currently.
 */
static Word readbyte2(LVM *self) {
    Byte msb = lua_nextbyte(self);
    Byte lsb = lua_nextbyte(self);
    return byteunmask(msb, 1) | lsb;
}

/**
 * Read the next 3 instructions and combine those 3 bytes into 1 24-bit operand.
 *
 * NOTE:
 *
 * This MUST be able to fit in a `DWord`.
 *
 * Compiler emitted them in this order: msb, mid, lsb. Since ip currently points
 * at msb, we can safely walk in this order.
 */
static DWord readbyte3(LVM *self) {
    Byte msb = lua_nextbyte(self);
    Byte mid = lua_nextbyte(self);
    Byte lsb = lua_nextbyte(self);
    return byteunmask(msb, 2) | byteunmask(mid, 1) | lsb;
}

/**
 * Read the next byte from the bytecode treating the received value as an index
 * into the VM's current chunk's constants pool.
 */
static TValue *readconstant_at(LVM *self, size_t index) {
    return &self->cf->function->chunk.constants.values[index];
}

static TValue *readconstant(LVM *self) {
    return readconstant_at(self, lua_nextbyte(self));
}

static TValue *readlconstant(LVM *self) {
    return readconstant_at(self, readbyte3(self));
}

void lua_pushobject(LVM *self, const TValue *object) {
    *self->sp = *object;
    self->sp++;
}

/**
 * III:21.2     Variable Declarations
 *
 * Helper macro to read the current top of the stack and increment the VM's
 * instruction pointer and then cast the result to a `TString*`.
 */
#define readstring_at(vm, i)    asstring(readconstant_at(vm, i))
#define readstring(vm)          asstring(readconstant(vm))
#define readlstring(vm)         asstring(readlconstant(vm))

/* }}} ---------------------------------------------------------------------- */

void lua_registerlib(LVM *self, const lua_Library library) {
    for (size_t i = 0; library[i].name != NULL; i++) {
        lua_pushliteral(self, library[i].name);   // Stack index 0.
        lua_pushcfunction(self, library[i].func); // Stack index 1.
        table_set(&self->globals, asstring(&self->stack[0]), self->stack[1]);
        lua_pop(self, 2);
    }
}

void lua_error(LVM *self, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    for (int i = self->fc - 1; i >= 0; i--) {
        const LFunction *function = self->frames[i].function;
        const Chunk *chunk        = &function->chunk;
        fprintf(stderr, "%s:%i: in ", self->name, current_line(chunk));
        if (function->name == NULL) {
            fprintf(stderr, "main chunk\n");
        } else {
            fprintf(stderr, "function '%s'\n", function->name->data);
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

TValue *lua_poke(LVM *self, int offset) {
    if (offset >= 0) {
        // Positive or zero offset in relation to base pointer, which may not
        // necessarily point to the bottom of the stack.
        TValue *value = self->bp + offset;
        return (value >= self->sp) ? &noneobject : value;
    } else {
        // Negative offset in relation to stack pointer.
        return self->sp + offset;
    }
}

size_t lua_gettop(LVM *self) {
    return self->sp - self->stack;
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

void lua_dojmp(LVM *self) {
    self->cf->ip += readbyte2(self);
}

void lua_dofjmp(LVM *self) {
    // Reading this regardless of falsiness is required to move to the next
    // non-jump instruction cleanly.
    Word jump = readbyte2(self);
    if (!lua_asboolean(self, -1)) {
        self->cf->ip += jump;
    }
}

void lua_doloop(LVM *self) {
    // Loops are just backwards jumps to the instruction for their condition.
    self->cf->ip -= readbyte2(self);
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
static bool call_luafunction(LVM *self, LFunction *luafn, int argc) {
    if (argc != luafn->arity) {
        lua_error(self, "Expected %i arguments but got %i.", luafn->arity, argc);
        return false;
    }
    // We want to iterate properly over something that has its own chunk, and as
    // a result its own lineruns. We do not do this for C functions as they do
    // not have any chunk, therefore no lineruns info, to begin with.
    luafn->chunk.prevline = 0;

    CallFrame *frame = &self->frames[self->fc++];
    frame->function = luafn;
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
    TValue *argv = self->sp - argc;
    const TValue res = cfn(self, argc, argv);
    self->sp -= argc + 1; // Point to slot right below the function object.
    lua_pushobject(self, &res);
    return true;
}

bool lua_call(LVM *self, int argc) {
    if (self->fc >= LUA_MAXFRAMES) {
        lua_error(self, "Stack overflow.");
        return false;
    }    
    // -1 to poke at top of stack, this is the function object itself.
    // In other words this is the base pointer of the next CallFrame.
    //
    // The function to be called was pushed first, then its arguments, then its
    // argument count.
    TValue *callee = self->sp - 1 - argc;
    if (callee->type != LUA_TFUNCTION) {
        const char *tname = lua_typename(self, callee->type);
        lua_error(self, "Attempt to call %s as function", tname);
        return false;
    }

    // Call the correct function in the union based on the boolean.
    if (asfunction(callee)->is_c) {
        return call_cfunction(self, ascfunction(callee), argc);
    } else {
        return call_luafunction(self, &asluafunction(callee), argc);
    }
}

bool lua_return(LVM *self) {
    // When a function returns a value, its result will be on the top of
    // the stack. We're about to discard the function's entire stack
    // window so we hold onto the return value.
    const TValue res = lua_peek(self, -1);
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

static void getglobal_at(LVM *self, bool islong) {
    TString *name = (islong) ? readlstring(self) : readstring(self);
    TValue value;
    // If not present in the hash table, the variable never existed.
    if (!table_get(&self->globals, name, &value)) {
        lua_error(self, "Undefined variable '%s'.", name->data);
    }
    lua_pushobject(self, &value);
}

void lua_getglobal(LVM *self) {
    getglobal_at(self, false);
}

void lua_getlglobal(LVM *self) {
    getglobal_at(self, true);
}

static void setglobal_at(LVM *self, bool islong) {
    TString *name = (islong) ? readlstring(self) : readstring(self);
    table_set(&self->globals, name, lua_peek(self, -1));
}

void lua_setglobal(LVM *self) {
    setglobal_at(self, false);
}

void lua_setlglobal(LVM *self) {
    setglobal_at(self, true);
}

void lua_getlocal(LVM *self) {
    const TValue *locals = self->cf->bp;
    const size_t index   = lua_nextbyte(self);
    lua_pushobject(self, &locals[index]);
}

void lua_setlocal(LVM *self) {
    TValue *locals = self->cf->bp;
    size_t index   = lua_nextbyte(self);
    locals[index]  = lua_peek(self, -1);
}

/* }}} ---------------------------------------------------------------------- */

VType lua_type(LVM *self, int offset) {
    return lua_poke(self, offset)->type;
}

const char *lua_typename(LVM *self, VType type) {
    unused(self);
    return get_tnameinfo(type)->what;
}

/* 'IS' FUNCTIONS ------------------------------------------------------- {{{ */

bool lua_iscfunction(LVM *self, int offset) {
    const TValue *v = lua_poke(self, offset);
    return isfunction(v) && iscfunction(v);
}

/* }}} */

bool lua_equal(LVM *self, int offset1, int offset2) {
    const TValue *lhs = lua_poke(self, offset1);
    const TValue *rhs = lua_poke(self, offset2);
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
    const TValue *v = lua_poke(self, offset);
    return !isfalsy(v);
}

lua_Number lua_asnumber(LVM *self, int offset) {
    const TValue *v = lua_poke(self, offset);
    return isnumber(v) ? asnumber(v) : (lua_Number)0;
}

TString *lua_aststring(LVM *self, int offset) {
    TValue *v = lua_poke(self, offset);
    return (isstring(v)) ? asstring(v) : NULL;
}

TFunction *lua_asfunction(LVM *self, int offset) {
    TValue *v = lua_poke(self, offset);
    return (isfunction(v)) ? asfunction(v) : NULL;
}

/* }}} --------------------------------------------------------------------- */

/* PUSH FUNCTIONS ------------------------------------------------------- {{{ */

void lua_pushconstant(LVM *self) {
    const TValue *constant = readconstant(self);
    lua_pushobject(self, constant);
}

void lua_pushlconstant(LVM *self) {
    const TValue *constant = readlconstant(self);
    lua_pushobject(self, constant);
}

void lua_pushboolean(LVM *self, bool b) {
    TValue v = makeboolean(b);
    lua_pushobject(self, &v);
}

void lua_pushnil(LVM *self) {
    lua_pushobject(self, &nilobject);
}

void lua_pushnumber(LVM *self, lua_Number n) {
    TValue v = makenumber(n);
    lua_pushobject(self, &v);
}

void lua_pushlstring(LVM *self, char *data, size_t len) {
    TValue v = makestring(take_string(self, data, len));
    lua_pushobject(self, &v);
}

void lua_pushstring(LVM *self, char *data) {
    if (data == NULL) {
        lua_pushnil(self);
    } else {
        lua_pushlstring(self, data, strlen(data));
    }
}

void lua_pushliteral(LVM *self, const char *data) {
    TValue v = makestring(copy_string(self, data, strlen(data)));
    lua_pushobject(self, &v);
}

void lua_pushfunction(LVM *self, TFunction *tfunc) {
    TValue v = makefunction(tfunc);
    lua_pushobject(self, &v);
}

void lua_pushcfunction(LVM *self, lua_CFunction function) {
    TFunction *tfunc = new_cfunction(self, function);
    lua_pushfunction(self, tfunc);
}

/* }}} */

void lua_concat(LVM *self) {
    TString *rhs = lua_aststring(self, -1);
    TString *lhs = lua_aststring(self, -2);
    if (lhs == NULL || rhs == NULL) {
        lua_binoperror(self, -2, -1, LUA_ERROR_CONCAT);
    }
    lua_pop(self, 2); // Clean up operands

    size_t len = lhs->len + rhs->len;
    char *data = allocate(char, len + 1);

    memcpy(&data[0],        lhs->data, lhs->len);
    memcpy(&data[lhs->len], rhs->data, rhs->len);
    data[len] = '\0';
    lua_pushlstring(self, data, len);
}
