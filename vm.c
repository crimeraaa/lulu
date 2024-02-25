#include <math.h>
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
    fprintf(stderr, "[line %i] in script\n", self->chunk->prevline);
    reset_vmptrs(self);
}

static inline InterpretResult 
runtime_arithmetic_error(lua_VM *self, TValue operand) {
    runtime_error(self, 
        "Attempt to perform arithmetic on a %s value", value_typename(operand));
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_comparison_error(lua_VM *self, TValue lhs, TValue rhs) {
    runtime_error(self, 
        "Attempt to compare %s with %s", value_typename(lhs), value_typename(rhs));
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_concatenation_error(lua_VM *self, TValue operand) {
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

void push_vmstack(lua_VM *self, TValue value) {
    *self->sp = value;
    self->sp++;
}

TValue pop_vmstack(lua_VM *self) {
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
 */
static inline TValue *poke_vmstack(lua_VM *self, int offset) {
    return self->sp - 1 - offset;
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
 */
static inline TValue peek_vmstack(lua_VM *self, int offset) {
    return *poke_vmstack(self, offset);
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
 */
static void concatenate(lua_VM *self) {
    lua_String *rhs = asstring(pop_vmstack(self));
    lua_String *lhs = asstring(pop_vmstack(self));
    int length      = lhs->length + rhs->length;
    char *data      = allocate(char, length + 1);
    memcpy(&data[0], lhs->data, lhs->length);
    memcpy(&data[lhs->length], rhs->data, rhs->length);
    data[length] = '\0';
    lua_String *result = take_string(self, data, length);
    push_vmstack(self, makeobject(LUA_TSTRING, result)); 
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
 * Read the next 3 instructions and combine those 3 bytes into 1 24-bit operand.
 * 
 * NOTE:
 *
 * Compiler emitted them in this order: hi, mid, lo. Since ip currently points
 * at hi, we can safely walk in this order.
 */
static inline DWord read_dword(lua_VM *self) {
    Byte hi  = read_byte(self); // bits 16..23 : (0x010000..0xFFFFFF)
    Byte mid = read_byte(self); // bits 8..15  : (0x000100..0x00FFFF)
    Byte lo  = read_byte(self); // bits 0..7   : (0x000000..0x0000FF)
    return (hi >> 16) | (mid >> 8) | (lo);
}


static InterpretResult run_bytecode(lua_VM *self) {
    // Hack, but we reset so we can disassemble properly.
    // We need that start with index 0 into the lines.runs array.
    // So effectively this becames our iterator.
    self->chunk->prevline = 0; 
    for (;;) {
        int offset = (int)(self->ip - self->chunk->code);
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (TValue *slot = self->bp; slot < self->sp; slot++) {
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
        Byte instruction;
        switch (instruction = read_byte(self)) {
        case OP_CONSTANT: {
            TValue value = read_constant(self);
            push_vmstack(self, value);
            break;
        }
        case OP_CONSTANT_LONG: {
            DWord index = read_dword(self);
            TValue value = read_constant_at(self, index);
            push_vmstack(self, value);
            break;
        }

        // -*- III:18.4     Two New Types ------------------------------------*-
        case OP_NIL:   push_vmstack(self, makenil); break;
        case OP_TRUE:  push_vmstack(self, makeboolean(true)); break;
        case OP_FALSE: push_vmstack(self, makeboolean(false)); break;
                       
        // -*- III:21.1.2   Expression statements ----------------------------*-
        case OP_POP: pop_vmstack(self); break;
                     
        // -*- III:21.2     Variable Declarations ----------------------------*-
        case OP_GET_GLOBAL: {
            lua_String *name = read_string(self);
            TValue value;
            // If not present in the hash table, the variable never existed.
            if (!table_get(&self->globals, name, &value)) {
                runtime_error(self, "Undefined variable '%s'.", name->data);
                return INTERPRET_RUNTIME_ERROR;
            }
            push_vmstack(self, value);
            break;
        }
        case OP_DEFINE_GLOBAL: {
            // Safe to assume this is a string given this instruction.
            lua_String *name = read_string(self);
            // Assign to the variable whatever's on top of the stack
            table_set(&self->globals, name, peek_vmstack(self, 0));
            pop_vmstack(self); // Don't pop until done in case we need to GC
            break;
        }
        case OP_DEFINE_GLOBAL_LONG: {
            DWord index = read_dword(self);
            lua_String *name = read_string_at(self, index);
            table_set(&self->globals, name, peek_vmstack(self, 0));
            pop_vmstack(self);
            break;
        }

        // -*- III:18.4.2   Equality and comparison operators ----------------*-
        case OP_EQ: {
            TValue rhs = pop_vmstack(self);
            TValue lhs = pop_vmstack(self);
            push_vmstack(self, makeboolean(values_equal(lhs, rhs)));
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
        // -*- III:21.1.1   Print statements ---------------------------------*-
        case OP_PRINT: {
            print_value(pop_vmstack(self));
            printf("\n");
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

#undef read_byte
#undef read_constant
#undef read_constant_at
#undef read_string
#undef assert_arithmetic
#undef assert_comparison
#undef binop_template
#undef binop_math
#undef binop_cmp
