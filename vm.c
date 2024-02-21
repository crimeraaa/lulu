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

void deinit_vm(LuaVM *self) {
    (void)self;
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
 * III:15.3.1   Binary Operators
 * 
 * Because C preprocessor macro metaprogramming sucks, I'm sorry in advance that
 * you have to see this mess!
 * 
 * @param vm        `LuaVM*`.
 * @param makefn    One of the `make*` macros.
 * @param operation One of the `lua_num*` macros or an actual function.
 */
#define binaryop(vm, makefn, operation) \
    do { \
        TValue rhs = pop_vmstack(vm); \
        TValue lhs = pop_vmstack(vm); \
        if (!isnumber(lhs)) return runtime_matherror(vm, typeof_value(lhs)); \
        if (!isnumber(rhs)) return runtime_matherror(vm, typeof_value(rhs)); \
        push_vmstack(vm, makefn(operation(lhs.as.number, rhs.as.number))); \
    } while(false)


static inline InterpretResult 
runtime_matherror(LuaVM *self, const char *type) {
    runtime_error(self, "Attempt to perform arithmetic on a %s value", type);
    return INTERPRET_RUNTIME_ERROR;
}

static InterpretResult run_bytecode(LuaVM *self) {
    self->chunk->prevline = 0; // Reset so we can disassemble properly.
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
            uint8_t hi  = read_byte(self);
            uint8_t mid = read_byte(self);
            uint8_t lo  = read_byte(self);
            // Construct a 24-bit integer out of 3 8-bit ones
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
        case OP_REL_EQ: {
            TValue rhs = pop_vmstack(self);
            TValue lhs = pop_vmstack(self);
            push_vmstack(self, makeboolean(values_equal(lhs, rhs)));
            break;
        }
        case OP_REL_GT: binaryop(self, makeboolean, lua_numgt); break;
        case OP_REL_LT: binaryop(self, makeboolean, lua_numlt); break;

        // -*- III:15.3.1   Binary Operators ---------------------------------*-
        case OP_ADD: binaryop(self, makenumber, lua_numadd); break;
        case OP_SUB: binaryop(self, makenumber, lua_numsub); break;
        case OP_MUL: binaryop(self, makenumber, lua_nummul); break;
        case OP_DIV: binaryop(self, makenumber, lua_numdiv); break;
        case OP_POW: binaryop(self, makenumber, lua_numpow); break;
        case OP_MOD: binaryop(self, makenumber, lua_nummod); break;
                     
        // -*- III:18.4.1   Logical not and falsiness ------------------------*-
        case OP_NOT: {
            // Doing this in-place to save some bytecode instructions
            TValue value = peek_vmstack(self, 0);
            *(self->sp - 1) = makeboolean(isfalsy(value));
            break;
        }

        // -*- III:15.3     An Arithmetic Calculator -------------------------*-
        case OP_UNM: {
            // Challenge 15.4: Negate in place
            TValue value = peek_vmstack(self, 0);
            if (isnumber(value)) {
                (self->sp - 1)->as.number = -value.as.number;
                break;
            } 
            return runtime_matherror(self, typeof_value(value));
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
    init_compiler(compiler);
    
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
#undef assert_math_op
#undef make_math_binaryop
#undef make_simple_binaryop
#undef binaryop
