#include <math.h>
#include "api.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

/** 
 * Make the VM's stack pointer point to the base of the stack array. 
 * This does the same for the base pointer.
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
        "Attempt to perform arithmetic on a %s value", 
        lua_typename(self, operand.type));
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_comparison_error(lua_VM *self, TValue lhs, TValue rhs) {
    runtime_error(self, 
        "Attempt to compare %s with %s", 
        lua_typename(self, lhs.type), 
        lua_typename(self, rhs.type));
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_concatenation_error(lua_VM *self, TValue operand) {
    runtime_error(self,
        "Attempt to concatenate a %s value", lua_typename(self, operand.type));
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
    lua_String *rhs = (lua_String*)lua_popvalue(self).as.object;
    lua_String *lhs = (lua_String*)lua_popvalue(self).as.object;
    int length      = lhs->length + rhs->length;
    char *data      = allocate(char, length + 1);
    memcpy(&data[0], lhs->data, lhs->length);
    memcpy(&data[lhs->length], rhs->data, rhs->length);
    data[length] = '\0';
    lua_String *result = take_string(self, data, length);
    lua_pushvalue(self, makeobject(LUA_TSTRING, result)); 
}

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
        TValue rhs = lua_popvalue(vm); \
        TValue lhs = lua_popvalue(vm); \
        assertfn(vm, lhs, rhs); \
        lua_pushvalue(vm, makefn(operation(lhs.as.number, rhs.as.number))); \
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

static InterpretResult run_bytecode(lua_VM *self) {
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
        Byte instruction;
        switch (instruction = lua_readbyte(self)) {
        case OP_CONSTANT: lua_pushconstant(self); break;
        case OP_CONSTANT_LONG: lua_pushconstant_long(self); break;

        // -*- III:18.4     Two New Types ------------------------------------*-
        case OP_NIL:   lua_pushnil(self); break;
        case OP_TRUE:  lua_pushboolean(self, true); break;
        case OP_FALSE: lua_pushboolean(self, false); break;
                       
        // -*- III:21.1.2   Expression statements ----------------------------*-
        case OP_POP:   lua_popvalue(self); break;
                     
        // -*- III:21.2     Variable Declarations ----------------------------*-
        case OP_GET_GLOBAL: {
            lua_String *name = lua_readstring(self);
            TValue value;
            // If not present in the hash table, the variable never existed.
            if (!table_get(&self->globals, name, &value)) {
                runtime_error(self, "Undefined variable '%s'.", name->data);
                return INTERPRET_RUNTIME_ERROR;
            }
            lua_pushvalue(self, value);
            break;
        }
        case OP_DEFINE_GLOBAL: {
            // Safe to assume this is a string given this instruction.
            lua_String *name = lua_readstring(self);
            // Assign to the variable whatever's on top of the stack
            table_set(&self->globals, name, lua_peekvalue(self, 0));
            lua_popvalue(self);
            break;
        }
        case OP_DEFINE_GLOBAL_LONG: {
            lua_String *name = lua_readstring_long(self);
            table_set(&self->globals, name, lua_peekvalue(self, 0));
            lua_popvalue(self);
            break;
        }

        // -*- III:18.4.2   Equality and comparison operators ----------------*-
        case OP_EQ: {
            TValue rhs = lua_popvalue(self);
            TValue lhs = lua_popvalue(self);
            lua_pushvalue(self, makeboolean(values_equal(lhs, rhs)));
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
            TValue rhs = lua_peekvalue(self, 0);
            TValue lhs = lua_peekvalue(self, 1);
            if (!isstring(lhs)) return runtime_concatenation_error(self, lhs);
            if (!isstring(rhs)) return runtime_concatenation_error(self, rhs);
            concatenate(self);
            break;
        }
                     
        // -*- III:18.4.1   Logical not and falsiness ------------------------*-
        case OP_NOT: {
            // Doing this in-place to save some bytecode instructions
            TValue value = lua_peekvalue(self, 0);
            *lua_pokevalue(self, 0) = makeboolean(isfalsy(value));
            break;
        }

        // -*- III:15.3     An Arithmetic Calculator -------------------------*-
        case OP_UNM: {
            // Challenge 15.4: Negate in place
            TValue value = lua_peekvalue(self, 0);
            if (isnumber(value)) {
                lua_pokevalue(self, 0)->as.number = -value.as.number;
                break;
            } 
            return runtime_arithmetic_error(self, value);
        }
        // -*- III:21.1.1   Print statements ---------------------------------*-
        case OP_PRINT: lua_print(self); break;
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

#undef assert_arithmetic
#undef assert_comparison
#undef binop_template
#undef binop_math
#undef binop_cmp
