#include <math.h>
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

/* Make the VM's stack pointer point to the base of the stack array. */
static inline void reset_vmsp(LuaVM *self) {
    self->sp = self->stack;
}

/**
 * III:18.3.1   Unary negation and runtime errors
 * 
 * This function simply prints whatever formatted error message you want.
 */
static void runtime_error(LuaVM *self, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    fprintf(stderr, "[line %i] in script\n", self->chunk->prevline);
    reset_vmsp(self);
}

static inline InterpretResult 
runtime_arithmetic_error(LuaVM *self, TValue operand) {
    runtime_error(self, 
        "Attempt to perform arithmetic on a %s value", lua_typename(operand));
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_comparison_error(LuaVM *self, TValue lhs, TValue rhs) {
    runtime_error(self, 
        "Attempt to compare %s with %s", lua_typename(lhs), lua_typename(rhs));
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_concatenation_error(LuaVM *self, TValue operand) {
    runtime_error(self,
        "Attempt to concatenate a %s value", lua_typename(operand));
    return INTERPRET_RUNTIME_ERROR;
}

void init_vm(LuaVM *self) {
    self->chunk = NULL;
    self->ip    = NULL;
    reset_vmsp(self);
    init_table(&self->strings);
    self->objects = NULL;
}

void deinit_vm(LuaVM *self) {
    deinit_table(&self->strings);
    free_objects(self);
}

void push_vmstack(LuaVM *self, TValue value) {
    *self->sp = value;
    self->sp++;
}

TValue pop_vmstack(LuaVM *self) {
    // 1 past top of stack was invalid, so now we actually point to top of stack
    // which is a valid element we can dereference!
    self->sp--;
    return *self->sp;
}

/**
 * III:18.3.1   Unary negation and runtime errors
 * 
 * Returns a value from the stack without popping it. Remember that since the
 * stack pointer points to 1 past the last element, we need to subtract 1.
 * And since the most recent element is at the top of the stack, in order to
 * access other elements we subtract the given distance.
 * 
 * For example, to peek the top of the stack, use `peek_vmstack(self, 0)`.
 * To peek the value right before that, use `peek_vmstack(self, 1)`. And so on.
 */
static inline TValue peek_vmstack(LuaVM *self, int distance) {
    return *(self->sp - 1 - distance);
}

/**
 * Similar to `peek_vmstack()` except you get a pointer to the slot in question.
 * This lets you manipulate the stack in place.
 */
static inline TValue *poke_vmstack(LuaVM *self, int distance) {
    return self->sp - 1 - distance;
}

/**
 * III:18.4.1   Logical not and falsiness
 * 
 * In Lua, the only "falsy" types are `nil` and the boolean value `false`.
 * Everything else is `truthy` meaning it is treated as a true condition.
 * Do note that this doesn't mean that `1 == true`, it just means that `if 1`
 * and `if true` do the same thing conceptually.
 */
static inline bool isfalsy(TValue value) {
    return isnil(value) || (isboolean(value) && !value.as.boolean);
}

/**
 * III:19.4.1   Concatenation
 * 
 * String concatenation is quite tricky due to all the allocations we need to
 * make! Not only that, but multiple concatenations may end up "orphaning"
 * middle strings and thus leaking memory.
 * 
 * III:19.1     Flexible Array Members (CHALLENGE)
 * 
 * I've effectively "shunted" responsibility of actually concatenating over to
 * `object.c:concat_strings()` just because it feels right for me.
 * Functionally this should not affect anything.
 */
static void concatenate(LuaVM *self) {
    lua_String *rhs    = asstring(pop_vmstack(self));
    lua_String *lhs    = asstring(pop_vmstack(self));
    lua_String *result = concat_strings(self, lhs, rhs);
    push_vmstack(self, makeobject(result));
}

/** 
 * Remember that postfix increment returns the original value of the expression. 
 * So we effectively increment the pointer but we dereference the original one.
 */
#define read_byte(vm)       (*(vm->ip++))

/**
 * Given 3 8-bit values representing one 24-bit integer, e.g:
 * `0b11010001_01110110_00111101`
 * 
 * 1. `x` is a bitmask of the leftmost 8-bit grouping, "upper"
 * 2. `y` is a bitmask of the middle 8-bit grouping, "middle"
 * 3. `z` is a bitmask of the rightmost 8-bit grouping, "lower"
 * 
 * We do the appropriate bit shiftings and bitwise operations to create a single
 * combined 24-bit/32-bit integer.
 */
// #define extract_int24(x, y, z)  ((x) >> 16) | ((y) >> 8) | (z)

/**
 * If you have an index greater than 8-bits, calculate that first however you
 * need to then use this macro so you have full control over all side effects.
 */
#define read_constant_at(vm, i) (vm->chunk->constants.values[i])

/**
 * Read the next byte from the bytecode treating the received value as an index
 * into the VM's current chunk's constants pool.
 */
#define read_constant(vm)       (read_constant_at(vm, read_byte(vm)))

/**
 * Horrible C preprocessor abuse...
 * 
 * @param vm        `LuaVM*`
 * @param assertfn  One of the `assert_*` macros.
 * @param makefn    One of the `make*` macros.
 * @param operation One of the `lua_num*` macros or an actual function.
 */
#define binop_template(vm, assertfn, makefn, operation) \
    do { \
        TValue rhs = pop_vmstack(vm); \
        TValue lhs = pop_vmstack(vm); \
        assertfn(vm, lhs, rhs); \
        push_vmstack(vm, makefn(operation(lhs.as.number, rhs.as.number))); \
    } while (false)

/**
 * We check lhs and rhs separately so we can report which one is likely to have
 * caused the error.
 */
#define assert_arithmetic(vm, lhs, rhs) \
    if (!isnumber(lhs)) return runtime_arithmetic_error(vm, lhs); \
    if (!isnumber(rhs)) return runtime_arithmetic_error(vm, rhs); \

/**
 * We check both lhs and rhs as you cannot compare non-numbers in terms of
 * greater-then and less-than.
 * 
 * This doesn't affect (and should not be used for) equality operators.
 * Meaning comparing equals-to and not-equals-to are valid calls.
 */
#define assert_comparison(vm, lhs, rhs) \
    if (!isnumber(lhs) || !isnumber(rhs)) { \
        return runtime_comparison_error(vm, lhs, rhs); \
    }

/**
 * III:15.3.1   Binary Operators
 * 
 * Because C preprocessor macro metaprogramming sucks, I'm sorry in advance that
 * you have to see this mess!
 * 
 * See the definition for `binop_template`.
 */
#define binop_math(vm, makefn, operation) \
    binop_template(vm, assert_arithmetic, makefn, operation)

/**
 * Similar to `binop_math`, only that it asserts both operands must be
 * numbers. This is because something like `1 > false` is invalid.
 * 
 * Note that this doesn't affect equality operators. `1 == false` is a valid call.
 */
#define binop_cmp(vm, makefn, operation) \
    binop_template(vm, assert_comparison, makefn, operation)


static InterpretResult run_bytecode(LuaVM *self) {
    // Hack, but we reset so we can disassemble properly.
    // We need that start with index 0 into the lines.runs array.
    // So effectively this becames our iterator.
    self->chunk->prevline = 0; 
    for (;;) {
        int offset = (int)(self->ip - self->chunk->code);
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (TValue *slot = self->stack; slot < self->sp; slot++) {
            printf("[ ");
            print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        // We need an integer byte offset from beginning of bytecode.
        disassemble_instruction(self->chunk, offset);
#else
        // If not doing debug traces, we still need to increment prevline so that
        // we can report what line an error occured in.
        get_instruction_line(self->chunk, offset);
#endif
        uint8_t instruction;
        switch (instruction = read_byte(self)) {
        case OP_CONSTANT: {
            TValue value = read_constant(self);
            push_vmstack(self, value);
            break;
        }
        case OP_CONSTANT_LONG: {
            uint8_t hi  = read_byte(self); // bits 16..23 represents (0x10..0x17)
            uint8_t mid = read_byte(self); // bits 8..15 represents (0x8..0xf)
            uint8_t lo  = read_byte(self); // bits 0..7 represents (0x0..0x7)
            int32_t index  = (hi >> 16) | (mid >> 8) | (lo);
            TValue value = read_constant_at(self, index);
            push_vmstack(self, value);
            break;
        }

        // -*- III:18.4     Two New Types ------------------------------------*-
        case OP_NIL:   push_vmstack(self, makenil); break;
        case OP_TRUE:  push_vmstack(self, makeboolean(true)); break;
        case OP_FALSE: push_vmstack(self, makeboolean(false)); break;
                       
        // -*- III:18.4.2   Equality and comparison operators ----------------*-
        case OP_EQ: {
            TValue rhs = pop_vmstack(self);
            TValue lhs = pop_vmstack(self);
            push_vmstack(self, makeboolean(values_equal(lhs, rhs)));
            break;
        }
        case OP_GT: binop_cmp(self, makeboolean, lua_numgt); break;
        case OP_LT: binop_cmp(self, makeboolean, lua_numlt); break;

        // -*- III:15.3.1   Binary Operators ---------------------------------*-
        case OP_ADD: binop_math(self, makenumber, lua_numadd); break;
        case OP_SUB: binop_math(self, makenumber, lua_numsub); break;
        case OP_MUL: binop_math(self, makenumber, lua_nummul); break;
        case OP_DIV: binop_math(self, makenumber, lua_numdiv); break;
        case OP_POW: binop_math(self, makenumber, lua_numpow); break;
        case OP_MOD: binop_math(self, makenumber, lua_nummod); break;
                     
        // -*- III:19.4.1   Concatenation ------------------------------------*-
        // This is repeating the code of `binop_math` but I really do not feel
        // like adding another macro parameter JUST to check a value type...
        case OP_CONCAT: {
            TValue rhs = peek_vmstack(self, 0);
            TValue lhs = peek_vmstack(self, 1);
            if (!isstring(lhs)) return runtime_concatenation_error(self, lhs);
            if (!isstring(rhs)) return runtime_concatenation_error(self, rhs);
            concatenate(self);
            break;
        }
                     
        // -*- III:18.4.1   Logical not and falsiness ------------------------*-
        case OP_NOT: {
            // Doing this in-place to save some bytecode instructions
            TValue *value = poke_vmstack(self, 0);
            *value = makeboolean(isfalsy(*value));
            break;
        }

        // -*- III:15.3     An Arithmetic Calculator -------------------------*-
        case OP_UNM: {
            // Challenge 15.4: Negate in place
            TValue *value = poke_vmstack(self, 0);
            if (isnumber(*value)) {
                value->as.number = -value->as.number;
                break;
            } 
            return runtime_arithmetic_error(self, *value);
        }
        case OP_RET: 
            print_value(pop_vmstack(self));
            printf("\n");
            return INTERPRET_OK;
        }
    }
}

InterpretResult interpret_vm(LuaVM *self, const char *source) {
    Compiler *compiler = &(Compiler){0};
    init_chunk(&compiler->chunk);
    init_compiler(compiler, self);
    
    if (!compile_bytecode(compiler, source)) {
        deinit_chunk(&compiler->chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    self->chunk = &compiler->chunk;
    self->ip    = compiler->chunk.code;
    InterpretResult result = run_bytecode(self);
    deinit_chunk(&compiler->chunk);

    return result;
}

#undef read_byte
#undef read_constant
#undef read_constant_at
#undef assert_arithmetic
#undef assert_comparison
#undef binop_template
#undef binop_math
#undef binop_cmp
