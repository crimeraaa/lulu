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
static inline void reset_stack(LVM *self) {
    self->sp = self->stack;
    self->fc = 0;
}

/**
 * III:18.3.1   Unary negation and runtime errors
 *
 * This function simply prints whatever formatted error message you want.
 */
static void runtime_error(LVM *self, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    
    const CallFrame *frame = &self->frames[self->fc - 1];
    const Chunk *chunk = &frame->function->chunk;
    int line = chunk->lines.runs[chunk->prevline - 1].where;
    fprintf(stderr, "[line %i] in script\n", line);
    reset_stack(self);
}

static inline InterpretResult
runtime_arith_error(LVM *self, int offset) {
    const char *typename = lua_typename(self, offset);
    runtime_error(self, "Attempt to perform arithmetic on a %s value", typename);
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_compare_error(LVM *self, int loffset, int roffset) {
    const char *ltypename = lua_typename(self, loffset);
    const char *rtypename = lua_typename(self, roffset);
    runtime_error(self, "Attempt to compare %s with %s", ltypename, rtypename);
    return INTERPRET_RUNTIME_ERROR;
}

static inline InterpretResult
runtime_concat_error(LVM *self, int offset) {
    const char *typename = lua_typename(self, offset);
    runtime_error(self, "Attempt to concatenate a %s value", typename);
    return INTERPRET_RUNTIME_ERROR;
}

void init_vm(LVM *self) {
    reset_stack(self);
    init_table(&self->globals);
    init_table(&self->strings);
    self->objects = NULL;
}

void free_vm(LVM *self) {
    free_table(&self->globals);
    free_table(&self->strings);
    free_objects(self);
}

TValue popstack(LVM *self) {
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
 * access other elements we subtract the given offset.
 *
 * For example, to peek the top of the stack, use `peekstack(self, 0)`.
 * To peek the value right before that, use `peekstack(self, 1)`. And so on.
 * 
 * III:23.3     While Statements
 * 
 * See notes for `pokestack()` on what was changed.
 * Basically: `peekstack(self, -1)` now gives you the top of the stack.
 * `peekstack(self, 0)` gives you the very bottom.
 */
static inline TValue peekstack(LVM *self, int offset) {
    return *lua_poke(self, offset);
}

/**
 * Read the current instruction and move the instruction pointer.
 *
 * Remember that postfix increment returns the original value of the expression.
 * So we effectively increment the pointer but we dereference the original one.
 */
static inline Byte read_byte(CallFrame *self) {
    return *(self->ip++);
}

/**
 * If you have an index greater than 8-bits, calculate that first however you
 * need to then use this macro so you have full control over all side effects.
 */
static inline TValue read_constant_at(CallFrame *self, size_t index) {
    return self->function->chunk.constants.values[index];
}

/**
 * Read the next byte from the bytecode treating the received value as an index
 * into the VM's current chunk's constants pool.
 */
#define read_constant(frame)        (read_constant_at(frame, read_byte(frame)))

/**
 * III:21.2     Variable Declarations
 *
 * Helper macro to read the current top of the stack and increment the VM's
 * instruction pointer and then cast the result to a `TString*`.
 */
#define read_string(frame)          asstring(read_constant(frame))
#define read_string_at(frame, i)    asstring(read_constant_at(frame, i))

#define pushvalue(vm, v)        (*(vm->sp++) = v)

/**
 * Horrible C preprocessor abuse...
 *
 * @param vm        `lua_VM*`
 * @param assertfn  One of the `assert_*` macros.
 * @param makefn    One of the `make*` macros.
 * @param exprfn    One of the `lua_num*` macros or an actual function.
 */
#define binop_template(vm, assertfn, makefn, exprfn)                           \
    do {                                                                       \
        assertfn(vm);                                                          \
        TValue r = makefn(exprfn(lua_asnumber(vm, -2), lua_asnumber(vm, -1))); \
        lua_popn(vm, 2);                                                       \
        lua_pushvalue(vm, r); \
    } while (false)

/**
 * We check lhs and rhs separately so we can report which one is likely to have
 * caused the error.
 */
#define assert_arithmetic(vm) \
    if (!lua_isnumber(vm, -2)) return runtime_arith_error(vm, -2); \
    if (!lua_isnumber(vm, -1)) return runtime_arith_error(vm, -1); \

/**
 * We check both lhs and rhs as you cannot compare non-numbers in terms of
 * greater-then and less-than.
 *
 * This doesn't affect (and should not be used for) equality operators.
 * Meaning comparing equals-to and not-equals-to are valid calls.
 */
#define assert_comparison(vm) \
    if (!lua_isnumber(vm, -2) || !lua_isnumber(vm, -1)) { \
        return runtime_compare_error(vm, -2, -1); \
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
static inline Word read_short(CallFrame *self) {
    Byte hi = read_byte(self);
    Byte lo = read_byte(self);
    return byteunmask(hi, 1) | lo;
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
static inline DWord read_long(CallFrame *self) {
    Byte hi  = read_byte(self); // bits 16..23 : (0x010000..0xFFFFFF)
    Byte mid = read_byte(self); // bits 8..15  : (0x000100..0x00FFFF)
    Byte lo  = read_byte(self); // bits 0..7   : (0x000000..0x0000FF)
    return byteunmask(hi, 2) | byteunmask(mid, 1) | lo;
}

static InterpretResult run_bytecode(LVM *self) {
    // Topmost call frame. 
    CallFrame *frame = &self->frames[self->fc - 1];

    // Hack, but we reset so we can disassemble properly.
    // We need that start with index 0 into the lines.runs array.
    // So effectively this becames our iterator.
    frame->function->chunk.prevline = 0;
    for (;;) {
        ptrdiff_t instruction_offset = frame->ip - frame->function->chunk.code;
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (const TValue *slot = self->stack; slot < self->sp; slot++) {
            printf("[ ");
            print_value(slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(&frame->function->chunk, instruction_offset);
#else
        // Even if not disassembling we still need this to report errors.
        get_instruction_line(self->chunk, offset);
#endif
        Byte instruction;
        switch (instruction = read_byte(frame)) {
        case OP_CONSTANT: {
            pushvalue(self, read_constant(frame));
            break;
        }
        case OP_LCONSTANT: {
            pushvalue(self, read_constant_at(frame, read_long(frame)));
            break;
        }

        // -*- III:18.4     Two New Types ------------------------------------*-
        case OP_NIL:   lua_pushnil(self);            break;
        case OP_TRUE:  lua_pushboolean(self, true);  break;
        case OP_FALSE: lua_pushboolean(self, false); break;

        // -*- III:21.1.2   Expression statements ----------------------------*-
        case OP_POP:   lua_popn(self, 1); break;
        case OP_NPOP: {
            // 1-byte operand is how much to decrement the stack pointer by.
            lua_popn(self, read_byte(frame));
            break;
        }

        // -*- III:22.4.1   Interpreting local variables ---------------------*-
        case OP_GETLOCAL: {
            Byte slot = read_byte(frame);
            pushvalue(self, frame->slots[slot]);
            break;
        }
        case OP_SETLOCAL: {
            Byte slot = read_byte(frame);
            frame->slots[slot] = *lua_poke(self, -1);
            lua_popn(self, 1); // Expression left stuff on top of the stack.
            break;
        }
        // -*- III:21.2     Variable Declarations ----------------------------*-
        // NOTE: As of III:21.4 I've removed the `OP_DEFINE*` opcodes and cases.
        case OP_GETGLOBAL: {
            TString *name = read_string(frame);
            TValue value;
            // If not present in the hash table, the variable never existed.
            if (!table_get(&self->globals, name, &value)) {
                runtime_error(self, "Undefined variable '%s'.", name->data);
                return INTERPRET_RUNTIME_ERROR;
            }
            pushvalue(self, value);
            break;
        }
        case OP_LGETGLOBAL: {
            TString *name = read_string_at(frame, read_long(frame));
            TValue value;
            if (!table_get(&self->globals, name, &value)) {
                runtime_error(self, "Undefined variable '%s'.", name->data);
                return INTERPRET_RUNTIME_ERROR;
            }
            pushvalue(self, value);
            break;
        }

        // -*- III:21.4     Assignment ---------------------------------------*-
        // Unlike in Lox, Lua allows implicit declaration of globals.
        // Also unlike Lox you simply can't type the equivalent of `var ident;`
        // in Lua as all global variables must be assigned at declaration.
        case OP_SETGLOBAL: {
            TString *name = read_string(frame);
            table_set(&self->globals, name, peekstack(self, -1));
            popstack(self);
            break;
        }
        case OP_LSETGLOBAL: {
            TString *name = read_string_at(frame, read_long(frame));
            table_set(&self->globals, name, peekstack(self, -1));
            popstack(self);
            break;
        }

        // -*- III:18.4.2   Equality and comparison operators ----------------*-
        case OP_EQ: {
            // Save result before popping so we can push properly.
            // -2 is lhs, -1 is rhs due to the order they were pushed in.
            bool res = lua_equal(self, -2, -1);
            lua_popn(self, 2);
            lua_pushboolean(self, res);
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
            if (!lua_isstring(self, -2)) return runtime_concat_error(self, -2);
            if (!lua_isstring(self, -1)) return runtime_concat_error(self, -1);
            lua_concat(self);
            break;
        }

        // -*- III:18.4.1   Logical not and falsiness ------------------------*-
        case OP_NOT: {
            *lua_poke(self, -1) = makeboolean(lua_isfalsy(self, -1));
            break;
        }

        // -*- III:15.3     An Arithmetic Calculator -------------------------*-
        case OP_UNM: {
            // Challenge 15.4: Negate in place
            if (lua_isnumber(self, -1)) {
                TValue *value    = lua_poke(self, -1);
                value->as.number = lua_numunm(value->as.number);
                break;
            }
            return runtime_arith_error(self, -1);
        }
        // -*- III:21.1.1   Print statements ---------------------------------*-
        case OP_PRINT: {
            const TValue *value = lua_poke(self, -1);
            print_value(value);
            lua_popn(self, 1);
            printf("\n");
            break;
        }

        // -*- III:23.1     If Statements ------------------------------------*-
        case OP_JMP: {
            Word offset = read_short(frame);
            frame->ip += offset;
            break;
        }
        case OP_FJMP: {
            Word offset = read_short(frame);
            if (lua_isfalsy(self, -1)) {
                frame->ip += offset;
            }
            break;
        }
                      
        // -*- III:23.3     While Statements ---------------------------------*-
        case OP_LOOP: {
            Word offset = read_short(frame);
            frame->ip -= offset;
            break;
        }
        case OP_RET:
            // Exit interpreter for now until we get functions going.
            lua_popn(self, 1); // Pop the script itself off the VM's stack
            return INTERPRET_OK;
        }
    }
}

InterpretResult interpret_vm(LVM *self, const char *source) {
    Compiler compiler;
    init_compiler(&compiler, self, FNTYPE_SCRIPT);
    Function *function = compile_bytecode(&compiler, source);
    if (function == NULL) {
        return INTERPRET_COMPILE_ERROR;
    }
    pushvalue(self, makeobject(LUA_TFUNCTION, function));
    CallFrame *frame = &self->frames[self->fc++];
    frame->function = function;
    frame->ip       = function->chunk.code; // very beginning of bytecode.
    frame->slots    = self->stack;          // very bottom of VM's stack.
    return run_bytecode(self);
}

#undef read_constant
#undef read_string
#undef read_string_at
#undef assert_arithmetic
#undef assert_comparison
#undef binop_template
#undef binop_math
#undef binop_cmp
