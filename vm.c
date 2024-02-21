#include <math.h>
#include "compiler.h"
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

void init_vm(LuaVM *self) {
    self->chunk = NULL;
    self->ip    = NULL;
    reset_vmsp(self);
}

void deinit_vm(LuaVM *self) {}

void push_vmstack(LuaVM *self, LuaValue value) {
    *self->sp = value;
    self->sp++;
}

LuaValue pop_vmstack(LuaVM *self) {
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
static inline LuaValue peek_vmstack(LuaVM *self, int distance) {
    return *(self->sp - 1 - distance);
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
#define extract_int24(x, y, z)  ((x) >> 16) | ((y) >> 8) | (z)

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

#define assert_math_op(vm, lhs, rhs) \
    if (!is_luanumber(lhs)) { \
        return runtime_matherror(vm, typeof_value(lhs)); \
    } else if (!is_luanumber(rhs)) { \
        return runtime_matherror(vm, typeof_value(rhs)); \
    }

#define make_math_binaryop(vm, ident1, ident2) \
    LuaValue ident2 = pop_vmstack(vm); \
    LuaValue ident1 = pop_vmstack(vm); \
    assert_math_op(vm, ident1, ident2); \

/**
 * III:15.3.1   Binary Operators
 * 
 * Because C preprocessor macro metaprogramming sucks, I'm sorry in advance that
 * you have to see this mess!
 * 
 * @param vm    `LuaVM*`.
 * @param mk    One of the `make_lua*` macros.
 * @param op    + - * / ^ %
 */
#define make_simple_binaryop(vm, mk, op) \
    do { \
        make_math_binaryop(vm, lhs, rhs); \
        push_vmstack(vm, mk(lhs.as.number op rhs.as.number)); \
    } while(false)

/**
 * In order to support modulo and exponents, we need to use the C math library.
 * So instead of passing a simple operation, you pass in a function that takes
 * 2 `double` and returns 1 `double`, e.g. `fmod()`, `pow()`.
 *
 * @param vm    `LuaVM*`.
 * @param mk    One of the `make_lua*` macros.
 * @param op    + - * / ^ %
 */
#define make_fncall_binaryop(vm, mk, fn) \
    do { \
        make_math_binaryop(vm, lhs, rhs); \
        push_vmstack(vm, mk(fn(lhs.as.number, rhs.as.number))); \
    } while(false)


static inline LuaInterpretResult 
runtime_matherror(LuaVM *self, const char *type) {
    runtime_error(self, "Attempt to perform arithmetic on a %s value", type);
    return INTERPRET_RUNTIME_ERROR;
}

static LuaInterpretResult run_bytecode(LuaVM *self) {
    for (;;) {
        int offset = (int)(self->ip - self->chunk->code);
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (LuaValue *slot = self->stack; slot < self->sp; slot++) {
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
            LuaValue value = read_constant(self);
            push_vmstack(self, value);
            break;
        }
        case OP_CONSTANT_LONG: {
            uint8_t upper  = read_byte(self);
            uint8_t middle = read_byte(self);
            uint8_t lower  = read_byte(self);
            int32_t index  = extract_int24(upper, middle, lower);
            LuaValue value = read_constant_at(self, index);
            push_vmstack(self, value);
            break;
        }
        // -*- III:18.4     Two New Types ------------------------------------*-
        case OP_NIL:   push_vmstack(self, make_luanil); break;
        case OP_TRUE:  push_vmstack(self, make_luaboolean(true)); break;
        case OP_FALSE: push_vmstack(self, make_luaboolean(false)); break;

        // -*- III:15.3.1   Binary Operators ---------------------------------*-
        case OP_ADD: make_simple_binaryop(self, make_luanumber, +); break;
        case OP_SUB: make_simple_binaryop(self, make_luanumber, -); break;
        case OP_MUL: make_simple_binaryop(self, make_luanumber, *); break;
        case OP_DIV: make_simple_binaryop(self, make_luanumber, /); break;
        case OP_POW: make_fncall_binaryop(self, make_luanumber, pow); break;
        case OP_MOD: make_fncall_binaryop(self, make_luanumber, fmod); break;

        // -*- III:15.3     An Arithmetic Calculator -------------------------*-
        case OP_UNM: {
            // Challenge 15.4: Negate in place
            LuaValue value = peek_vmstack(self, 0);
            if (is_luanumber(value)) {
                (self->sp - 1)->as.number = -value.as.number;
                break;
            } else {
                return runtime_matherror(self, typeof_value(value));
            }
        }
        case OP_RET: 
            print_value(pop_vmstack(self));
            printf("\n");
            return INTERPRET_OK;            
        }
    }
}

LuaInterpretResult interpret_vm(LuaVM *self, const char *source) {
    LuaCompiler *compiler = &(LuaCompiler){0};
    init_chunk(&compiler->chunk);
    init_compiler(compiler);
    
    if (!compile_bytecode(compiler, source)) {
        deinit_chunk(&compiler->chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    self->chunk = &compiler->chunk;
    self->ip    = compiler->chunk.code;
    // Reset so we can disassemble properly.
    self->chunk->prevline = 0;
    LuaInterpretResult result = run_bytecode(self);
    deinit_chunk(&compiler->chunk);

    return result;
}

#undef extract_int24
#undef read_byte
#undef read_constant
#undef read_constant_at
#undef assert_math_op
#undef make_math_binaryop
#undef make_simple_binaryop
#undef make_fncall_binaryop
