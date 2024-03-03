#include <math.h>
#include "api.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

/**
 * Make the VM's stack pointer point to the base of the stack array.
 * The same is done for the base pointer.
 */
static inline void reset_vmptrs(lua_VM *self) {
    self->bp = self->stack;
    self->sp = self->stack;
}

/**
 * III:18.3.1   Unary negation and runtime errors
 *
 * This function simply prints whatever formatted error message you want.
 */
static void runtime_error(lua_VM *self, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    int line = self->chunk->lines.runs[self->chunk->prevline - 1].where;
    fprintf(stderr, "[line %i] in script\n", line);
    reset_vmptrs(self);
}

static inline InterpretResult
runtime_arith_error(lua_VM *self, const TValue *operand) {
    runtime_error(self,
        "Attempt to perform arithmetic on a %s value", value_typename(operand));
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_compare_error(lua_VM *self, const TValue *lhs, const TValue *rhs) {
    runtime_error(self,
        "Attempt to compare %s with %s", value_typename(lhs), value_typename(rhs));
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_concat_error(lua_VM *self, const TValue *operand) {
    runtime_error(self,
        "Attempt to concatenate a %s value", value_typename(operand));
    return INTERPRET_RUNTIME_ERROR;
}

void init_vm(lua_VM *self) {
    self->chunk = NULL;
    self->ip    = NULL;
    reset_vmptrs(self);
    init_table(&self->globals);
    init_table(&self->strings);
    self->objects = NULL;
}

void free_vm(lua_VM *self) {
    free_table(&self->globals);
    free_table(&self->strings);
    free_objects(self);
}

void pushstack(lua_VM *self, TValue value) {
    *self->sp = value;
    self->sp++;
}

TValue popstack(lua_VM *self) {
    // 1 past top of stack was invalid, so now we actually point to top of stack
    // which is a valid element we can dereference!
    self->sp--;
    return *self->sp;
}

/**
 * Similar to `peek_vmstack()` except you get a pointer to the slot in question.
 * This lets you manipulate the stack in place.
 *
 * NOTE:
 *
 * This is a negative offset in relation to the stack pointer. So an offset of 0
 * for example refers to the top of the stack, an offset of 1 is the slot right
 * below the top of the stack, etc. etc.
 * 
 * III:23.3     While Statements
 * 
 * I'm updating the semantics so that it looks more like the Lua C API.
 * `offset` can now be a negative offset in relation to the stack pointer,
 * but it can also be a positive offset in relation to the base pointer.
 * e.g. `-1` means top of stack, `0` means very bottom, `1` means 1 past bottom.
 */
static inline TValue *pokestack(lua_VM *self, int offset) {
    if (offset < 0) {
        return self->sp + offset; // Is negative so we can add to go downwards.
    } else {
        return self->bp + offset;
    }
}

/**
 * III:18.3.1   Unary negation and runtime errors
 *
 * Returns a value from the stack without popping it. Remember that since the
 * stack pointer points to 1 past the last element, we need to subtract 1.
 * And since the most recent element is at the top of the stack, in order to
 * access other elements we subtract the given offset.
 *
 * For example, to peek the top of the stack, use `peek_vmstack(self, 0)`.
 * To peek the value right before that, use `peek_vmstack(self, 1)`. And so on.
 * 
 * III:23.3     While Statements
 * 
 * See notes for `pokestack()` on what was changed.
 * Basically: `peek_vmstack(self, -1)` now gives you the top of the stack.
 * `peek_vmstack(self, 0)` gives you the very bottom.
 */
static inline TValue peek_vmstack(lua_VM *self, int offset) {
    return *pokestack(self, offset);
}

/**
 * III:18.4.1   Logical not and falsiness
 *
 * In Lua, the only "falsy" types are `nil` and the boolean value `false`.
 * Everything else is `truthy` meaning it is treated as a true condition.
 * Do note that this doesn't mean that `1 == true`, it just means that `if 1`
 * and `if true` do the same thing conceptually.
 * 
 * III:23.3     While Statements
 * 
 * I'm updating the API to be more like the Lua C API. So we take a VM instance
 * pointer and an offset into it, we determine the falsiness of the value at the
 * given index/offset.
 */
static inline bool isfalsy(const lua_VM *self, int offset) {
    size_t i = lua_absindex(self, offset);
    if (lua_isnil(self, i)) {
        return true;
    }
    return lua_isboolean(self, i) && !lua_asboolean(self, i);
}

/**
 * III:19.4.1   Concatenation
 *
 * String concatenation is quite tricky due to all the allocations we need to
 * make! Not only that, but multiple concatenations may end up "orphaning"
 * middle strings and thus leaking memory.
 */
static void concatenate(lua_VM *self) {
    lua_String *rhs = asstring(popstack(self));
    lua_String *lhs = asstring(popstack(self));
    size_t len = lhs->len + rhs->len;
    char *data = allocate(char, len + 1);
    memcpy(&data[0],        lhs->data, lhs->len);
    memcpy(&data[lhs->len], rhs->data, rhs->len);
    data[len] = '\0';
    lua_String *result = take_string(self, data, len);
    pushstack(self, makeobject(LUA_TSTRING, result));
}

/**
 * Read the current instruction and move the instruction pointer.
 *
 * Remember that postfix increment returns the original value of the expression.
 * So we effectively increment the pointer but we dereference the original one.
 */
static inline Byte read_byte(lua_VM *self) {
    return *(self->ip++);
}

/**
 * If you have an index greater than 8-bits, calculate that first however you
 * need to then use this macro so you have full control over all side effects.
 */
static inline TValue read_constant_at(lua_VM *self, QWord index) {
    return self->chunk->constants.values[index];
}

/**
 * Read the next byte from the bytecode treating the received value as an index
 * into the VM's current chunk's constants pool.
 */
#define read_constant(vm)       (read_constant_at(vm, read_byte(vm)))

/**
 * III:21.2     Variable Declarations
 *
 * Helper macro to read the current top of the stack and increment the VM's
 * instruction pointer and then cast the result to a `lua_String*`.
 */
#define read_string(vm)         asstring(read_constant(vm))
#define read_string_at(vm, i)   asstring(read_constant_at(vm, i))

/**
 * Horrible C preprocessor abuse...
 *
 * @param vm        `lua_VM*`
 * @param assertfn  One of the `assert_*` macros.
 * @param makefn    One of the `make*` macros.
 * @param operation One of the `lua_num*` macros or an actual function.
 */
#define binop_template(vm, assertfn, makefn, operation) \
    do { \
        TValue rhs = popstack(vm); \
        TValue lhs = popstack(vm); \
        assertfn(vm, lhs, rhs); \
        pushstack(vm, makefn(operation(lhs.as.number, rhs.as.number))); \
    } while (false)

/**
 * We check lhs and rhs separately so we can report which one is likely to have
 * caused the error.
 */
#define assert_arithmetic(vm, lhs, rhs) \
    if (!isnumber(lhs)) return runtime_arith_error(vm, &lhs); \
    if (!isnumber(rhs)) return runtime_arith_error(vm, &rhs); \

/**
 * We check both lhs and rhs as you cannot compare non-numbers in terms of
 * greater-then and less-than.
 *
 * This doesn't affect (and should not be used for) equality operators.
 * Meaning comparing equals-to and not-equals-to are valid calls.
 */
#define assert_comparison(vm, lhs, rhs) \
    if (!isnumber(lhs) || !isnumber(rhs)) { \
        return runtime_compare_error(vm, &lhs, &rhs); \
    }

/**
 * III:15.3.1   Binary Operators
 *
 * Because C preprocessor macro metaprogramming sucks, I'm sorry in advance that
 * you have to see this mess!
 *
 * See the definition for `binop_template`.
 */
#define binop_math(vm, operation) \
    binop_template(vm, assert_arithmetic, makenumber, operation)

/**
 * Similar to `binop_math`, only that it asserts both operands must be
 * numbers. This is because something like `1 > false` is invalid.
 *
 * Note that this doesn't affect equality operators. `1 == false` is a valid call.
 */
#define binop_cmp(vm, operation) \
    binop_template(vm, assert_comparison, makeboolean, operation)

/**
 * III:23.1     If Statements
 *
 * Read the next 2 instructions and combine them into a 16-bit operand.
 *
 * The compiler emitted the 2 byte operands for a jump instruction in order of
 * hi, lo. So our instruction pointer points at hi currently.
 */
static inline Word readshort(lua_VM *self) {
    Byte hi = read_byte(self);
    Byte lo = read_byte(self);
    return (hi << bitsize(Byte)) | lo;
}

/**
 * Read the next 3 instructions and combine those 3 bytes into 1 24-bit operand.
 *
 * NOTE:
 *
 * This MUST be able to fit in a `DWord`.
 *
 * Compiler emitted them in this order: hi, mid, lo. Since ip currently points
 * at hi, we can safely walk in this order.
 */
static inline DWord read_long(lua_VM *self) {
    Byte hi  = read_byte(self); // bits 16..23 : (0x010000..0xFFFFFF)
    Byte mid = read_byte(self); // bits 8..15  : (0x000100..0x00FFFF)
    Byte lo  = read_byte(self); // bits 0..7   : (0x000000..0x0000FF)
    return (hi << bitsize(Word)) | (mid << bitsize(Byte)) | (lo);
}

static InterpretResult run_bytecode(lua_VM *self) {
    // Hack, but we reset so we can disassemble properly.
    // We need that start with index 0 into the lines.runs array.
    // So effectively this becames our iterator.
    self->chunk->prevline = 0;
    for (;;) {
        ptrdiff_t instruction_offset = self->ip - self->chunk->code;
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (const TValue *slot = self->bp; slot < self->sp; slot++) {
            printf("[ ");
            print_value(slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(self->chunk, instruction_offset);
#else
        // Even if not disassembling we still need this to report errors.
        get_instruction_line(self->chunk, offset);
#endif
        Byte instruction;
        switch (instruction = read_byte(self)) {
        case OP_CONSTANT: {
            TValue value = read_constant(self);
            pushstack(self, value);
            break;
        }
        case OP_LCONSTANT: {
            TValue value = read_constant_at(self, read_long(self));
            pushstack(self, value);
            break;
        }

        // -*- III:18.4     Two New Types ------------------------------------*-
        case OP_NIL:   pushstack(self, makenil); break;
        case OP_TRUE:  pushstack(self, makeboolean(true)); break;
        case OP_FALSE: pushstack(self, makeboolean(false)); break;

        // -*- III:21.1.2   Expression statements ----------------------------*-
        case OP_POP:   lua_pop(self, 1); break;
        case OP_NPOP: {
            // 1-byte operand is how much to decrement the stack pointer by.
            lua_pop(self, read_byte(self));
            break;
        }

        // -*- III:22.4.1   Interpreting local variables ---------------------*-
        case OP_GETLOCAL: {
            Byte slot = read_byte(self);
            pushstack(self, self->stack[slot]);
            break;
        }
        case OP_SETLOCAL: {
            Byte slot = read_byte(self);
            self->stack[slot] = peek_vmstack(self, -1);
            popstack(self); // Expression left stuff on top of the stack.
            break;
        }
        // -*- III:21.2     Variable Declarations ----------------------------*-
        // NOTE: As of III:21.4 I've removed the `OP_DEFINE*` opcodes and cases.
        case OP_GETGLOBAL: {
            lua_String *name = read_string(self);
            TValue value;
            // If not present in the hash table, the variable never existed.
            if (!table_get(&self->globals, name, &value)) {
                runtime_error(self, "Undefined variable '%s'.", name->data);
                return INTERPRET_RUNTIME_ERROR;
            }
            pushstack(self, value);
            break;
        }
        case OP_LGETGLOBAL: {
            lua_String *name = read_string_at(self, read_long(self));
            TValue value;
            if (!table_get(&self->globals, name, &value)) {
                runtime_error(self, "Undefined variable '%s'.", name->data);
                return INTERPRET_RUNTIME_ERROR;
            }
            pushstack(self, value);
            break;
        }

        // -*- III:21.4     Assignment ---------------------------------------*-
        // Unlike in Lox, Lua allows implicit declaration of globals.
        // Also unlike Lox you simply can't type the equivalent of `var ident;`
        // in Lua as all global variables must be assigned at declaration.
        case OP_SETGLOBAL: {
            lua_String *name = read_string(self);
            table_set(&self->globals, name, peek_vmstack(self, -1));
            popstack(self);
            break;
        }
        case OP_LSETGLOBAL: {
            lua_String *name = read_string_at(self, read_long(self));
            table_set(&self->globals, name, peek_vmstack(self, -1));
            popstack(self);
            break;
        }

        // -*- III:18.4.2   Equality and comparison operators ----------------*-
        case OP_EQ: {
            TValue rhs = popstack(self);
            TValue lhs = popstack(self);
            pushstack(self, makeboolean(values_equal(&lhs, &rhs)));
            break;
        }
        case OP_GT: binop_cmp(self, lua_numgt); break;
        case OP_LT: binop_cmp(self, lua_numlt); break;

        // -*- III:15.3.1   Binary Operators ---------------------------------*-
        case OP_ADD: binop_math(self, lua_numadd); break;
        case OP_SUB: binop_math(self, lua_numsub); break;
        case OP_MUL: binop_math(self, lua_nummul); break;
        case OP_DIV: binop_math(self, lua_numdiv); break;
        case OP_POW: binop_math(self, lua_numpow); break;
        case OP_MOD: binop_math(self, lua_nummod); break;

        // -*- III:19.4.1   Concatenation ------------------------------------*-
        // This is repeating the code of `binop_math` but I really do not feel
        // like adding another macro parameter JUST to check a value type...
        case OP_CONCAT: {
            if (!lua_isstring(self, -2)) {
                return runtime_concat_error(self, pokestack(self, -2));
            }
            if (!lua_isstring(self, -1)) {
                return runtime_concat_error(self, pokestack(self, -1));
            }
            concatenate(self);
            break;
        }

        // -*- III:18.4.1   Logical not and falsiness ------------------------*-
        case OP_NOT: {
            *pokestack(self, -1) = makeboolean(isfalsy(self, -1));
            break;
        }

        // -*- III:15.3     An Arithmetic Calculator -------------------------*-
        case OP_UNM: {
            // Challenge 15.4: Negate in place
            TValue *value = pokestack(self, -1);
            if (lua_isnumber(self, -1)) {
                value->as.number = lua_numunm(value->as.number);
                break;
            }
            return runtime_arith_error(self, value);
        }
        // -*- III:21.1.1   Print statements ---------------------------------*-
        case OP_PRINT: {
            TValue value = popstack(self);
            print_value(&value);
            printf("\n");
            break;
        }

        // -*- III:23.1     If Statements ------------------------------------*-
        // This is an unconditional jump.
        case OP_JMP: {
            Word offset = readshort(self);
            self->ip += offset;
            break;
        }
        case OP_FJMP: {
            Word offset = readshort(self);
            if (isfalsy(self, -1)) {
                self->ip += offset;
            }
            break;
        }
                      
        // -*- III:23.3     While Statements ---------------------------------*-
        case OP_LOOP: {
            Word offset = readshort(self);
            self->ip -= offset;
            break;
        }
        case OP_RET:
            // Exit interpreter for now until we get functions going.
            return INTERPRET_OK;
        }
    }
}

InterpretResult interpret_vm(lua_VM *self, const char *source) {
    Compiler *compiler = &(Compiler){0};
    init_chunk(&compiler->chunk);
    init_compiler(compiler, self);
    if (!compile_bytecode(compiler, source)) {
        free_chunk(&compiler->chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    self->chunk = &compiler->chunk;
    self->ip    = compiler->chunk.code;
    InterpretResult result = run_bytecode(self);
    free_chunk(&compiler->chunk);
    return result;
}

#undef read_constant
#undef read_string
#undef read_string_at
#undef assert_arithmetic
#undef assert_comparison
#undef binop_template
#undef binop_math
#undef binop_cmp
