#include <math.h>
#include "vm.h"

/* Make the VM's stack pointer point to the base of the stack array. */
static inline void reset_stackpointer(LuaVM *self) {
    self->sp = self->stack;
}

void init_vm(LuaVM *self) {
    self->chunk = NULL;
    self->ip    = NULL;
    reset_stackpointer(self);
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

#define assert_number_op(lhs, rhs) \
    do { \
        if (!is_luanumber(lhs) || !is_luanumber(rhs)) { \
            printf("Both operands must be numbers.\n"); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
    } while(false) \

/**
 * III:15.3.1   Binary Operators
 * 
 * Because C preprocessor macro metaprogramming sucks, I'm sorry in advance that
 * you have to see this mess!
 */
#define make_simple_binaryop(vm, op) \
    do { \
        LuaValue rhs = pop_vmstack(vm); LuaValue lhs = pop_vmstack(vm); \
        assert_number_op(lhs, rhs); \
        push_vmstack(vm, make_luanumber(lhs.as.number op rhs.as.number)); \
    } while(false)

/**
 * In order to support modulo and exponents, we need to use the C math library.
 * So instead of passing a simple operation, you pass in a function that takes
 * 2 `double` and returns 1 `double`, e.g. `fmod()`, `pow()`.
 */
#define make_fncall_binaryop(vm, fn) \
    do { \
        LuaValue rhs = pop_vmstack(vm); LuaValue lhs = pop_vmstack(vm); \
        assert_number_op(lhs, rhs); \
        push_vmstack(vm, make_luanumber(fn(lhs.as.number, rhs.as.number))); \
    } while(false)


static LuaInterpretResult run_bytecode(LuaVM *self) {
    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (LuaValue *slot = self->stack; slot < self->sp; slot++) {
            printf("[ ");
            print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        // We need an integer byte offset from beginning of bytecode.
        disassemble_instruction(self->chunk, (int)(self->ip - self->chunk->code));
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
        // -*- III:15.3.1   Binary Operators ---------------------------------*-
        case OP_ADD: make_simple_binaryop(self, +); break;
        case OP_SUB: make_simple_binaryop(self, -); break;
        case OP_MUL: make_simple_binaryop(self, *); break;
        case OP_DIV: make_simple_binaryop(self, /); break;
        case OP_POW: make_fncall_binaryop(self, pow); break;
        case OP_MOD: make_fncall_binaryop(self, fmod); break;

        // -*- III:15.3     An Arithmetic Calculator -------------------------*-
        case OP_UNM: {
            // Challenge 15.4: Negate in place
            LuaValue *value = self->sp - 1;
            if (is_luanumber(*value)) {
                value->as.number = -(value->as.number);
                break;
            } else {
                print_value(*value);
                printf(" is not a Lua number and cannot be negated!\n");
                return INTERPRET_RUNTIME_ERROR;
            }
        }
        case OP_RET: 
            print_value(pop_vmstack(self));
            printf("\n");
            return INTERPRET_OK;            
        }
    }
}

LuaInterpretResult interpret_vm(LuaVM *self, LuaChunk *chunk) {
    self->chunk = chunk;
    self->ip    = chunk->code;
    // Need in case we call disassemble_instruction() w/o disassemble_chunk()
    self->chunk->prevline = -1; 
    return run_bytecode(self);
}

#undef extract_int24
#undef read_byte
#undef read_constant
#undef read_constant_at
