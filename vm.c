#include "api.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

/* HELPERS -------------------------------------------------------------- {{{ */

/**
 * Make the VM's stack pointer point to the base of the stack array.
 * The same is done for the base pointer.
 */
static void reset_stack(LVM *self) {
    self->bp = self->stack;
    self->sp = self->stack;
    self->fc = 0;
}

static void intern_identifiers(LVM *self) {
    for (VType tt = (VType)0; tt < LUA_TCOUNT; tt++) {
        const TNameInfo *tname = get_tnameinfo(tt);
        // Don't need to call `table_set()` since this implicitly already does
        // that for us.
        copy_string(self, tname->what, tname->len);
    }
}

void init_vm(LVM *self, const char *name) {
    init_table(&self->globals);
    init_table(&self->strings);
    reset_stack(self);
    self->objects = NULL;
    self->name    = name;
    self->_G      = maketable(&self->globals);
    // Store a reference to our globals table in the global variable `_G`.
    lua_pushtable(self, &self->globals);
    lua_setglobal(self, "_G");
    intern_identifiers(self);
}

void free_vm(LVM *self) {
    free_table(&self->globals);
    free_table(&self->strings);
    free_objects(self);
}

/**
 * Horrible C preprocessor abuse...
 *
 * @param vm        `lua_VM*`
 * @param T         Expected type of result.
 * @param assertfn  One of the `assert_*` macros.
 * @param exprfn    One of the `lua_num*` macros or an actual function.
 * @param pushfn    One of `lua_pushnumber` or `lua_pushboolean`.
 */
#define binop_template(vm, T, assertfn, exprfn, pushfn)                        \
    do {                                                                       \
        assertfn(vm);                                                          \
        T res = exprfn(lua_asnumber(vm, -2), lua_asnumber(vm, -1));            \
        lua_pop(vm, 2);                                                        \
        pushfn(vm, res);                                                       \
    } while (false)

/**
 * We check lhs and rhs separately so we can report which one is likely to have
 * caused the error.
 */
#define assert_arithmetic(vm)                                                  \
    if (!lua_isnumber(vm, -2)) lua_unoperror(vm, -2, LUA_ERROR_ARITH);         \
    if (!lua_isnumber(vm, -1)) lua_unoperror(vm, -1, LUA_ERROR_ARITH);         \

/**
 * We check both lhs and rhs as you cannot compare non-numbers in terms of
 * greater-then and less-than.
 *
 * This doesn't affect (and should not be used for) equality operators.
 * Meaning comparing equals-to and not-equals-to are valid calls.
 */
#define assert_comparison(vm)                                                  \
    if (!lua_isnumber(vm, -2) || !lua_isnumber(vm, -1)) {                      \
        lua_binoperror(vm, -2, -1, LUA_ERROR_COMPARE);                         \
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
    binop_template(vm, lua_Number, assert_arithmetic, operation, lua_pushnumber)

/**
 * Similar to `binop_math`, only that it asserts both operands must be
 * numbers. This is because something like `1 > false` is invalid.
 *
 * Note that this doesn't affect equality operators. `1 == false` is a valid call.
 */
#define binop_cmp(vm, operation) \
    binop_template(vm, bool, assert_comparison, operation, lua_pushboolean)

static InterpretResult run_bytecode(LVM *self) {
    Chunk *chunk = &self->cf->function->chunk;
    // longjmp here to handle errors.
    // Ensure that all functions that need manual cleanup have been taken care 
    // of, but for the most part our VM's objects linked list tracks ALL 
    // allocations no matter what.
    if (setjmp(self->errjmp) != 0) {
        reset_stack(self);
        return INTERPRET_RUNTIME_ERROR;
    }

    // Hack, but we reset so we can disassemble properly.
    // We need that start with index 0 into the lines.runs array.
    // So effectively this becames our iterator for each function frame.
    for (;;) {
        ptrdiff_t byteoffset = self->cf->ip - chunk->code;
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (const TValue *slot = self->bp; slot < self->sp; slot++) {
            printf("[ ");
            print_value(slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(chunk, byteoffset);
#else
        // Even if not disassembling we still need this to report errors.
        next_line(chunk, byteoffset);
#endif
        Byte instruction;
        switch (instruction = lua_nextbyte(self)) {
        case OP_CONSTANT:   lua_pushconstant(self);     break;
        case OP_LCONSTANT:  lua_pushlconstant(self);    break;

        // -*- III:18.4     Two New Types ------------------------------------*-
        case OP_NIL:        lua_pushnil(self);              break;
        case OP_TRUE:       lua_pushboolean(self, true);    break;
        case OP_FALSE:      lua_pushboolean(self, false);   break;

        // -*- III:21.1.2   Expression statements ----------------------------*-
        case OP_POP:        lua_pop(self, 1);                   break;
        case OP_NPOP:       lua_pop(self, lua_nextbyte(self));  break;

        // -*- III:22.4.1   Interpreting local variables ---------------------*-
        case OP_GETLOCAL: {
            const TValue *locals = self->cf->bp;
            const size_t index   = lua_nextbyte(self);
            lua_pushobject(self, &locals[index]);
        } break;
 
        case OP_SETLOCAL: {
            TValue *locals = self->cf->bp;
            size_t index   = lua_nextbyte(self);
            locals[index]  = lua_peek(self, -1);
            lua_pop(self, 1);
        } break;

        // -*- III:21.2     Variable Declarations ----------------------------*-
        // NOTE: As of III:21.4 I've removed the `OP_DEFINE*` opcodes and cases.
        case OP_GETGLOBAL:  {
            const char *name = ascstring(readconstant(self));
            lua_getglobal(self, name);
        } break;

        case OP_LGETGLOBAL: {
            const char *name = ascstring(readlconstant(self));
            lua_getglobal(self, name);
        } break;

        // -*- III:21.4     Assignment ---------------------------------------*-
        // Unlike in Lox, Lua allows implicit declaration of globals.
        // Also unlike Lox you simply can't type the equivalent of `var ident;`
        // in Lua as all global variables must be assigned at declaration.
        case OP_SETGLOBAL:  {
            const char *name = ascstring(readconstant(self));
            lua_setglobal(self, name);
        } break;

        case OP_LSETGLOBAL: {
            const char *name = ascstring(readlconstant(self));
            lua_setglobal(self, name);
        } break;

        // -*- III:18.4.2   Equality and comparison operators ----------------*-
        case OP_EQ: {
            // Save result before popping so we can push properly.
            // -2 is lhs, -1 is rhs due to the order they were pushed in.
            bool res = lua_equal(self, -2, -1);
            lua_pop(self, 2);
            lua_pushboolean(self, res);
        } break;

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
            lua_concat(self);
        } break;

        // -*- III:18.4.1   Logical not and falsiness ------------------------*-
        case OP_NOT: {
            bool res = lua_asboolean(self, -1);
            *lua_poke(self, -1) = makeboolean(!res);
        } break;

        // -*- III:15.3     An Arithmetic Calculator -------------------------*-
        case OP_UNM: {
            // Challenge 15.4: Negate in place
            if (lua_isnumber(self, -1)) {
                TValue *value    = lua_poke(self, -1);
                value->as.number = lua_numunm(value->as.number);
                break;
            }
            lua_unoperror(self, -1, LUA_ERROR_ARITH);
            break;
        }

        // -*- III:23.1     If Statements ------------------------------------*-
        case OP_JMP: {
            self->cf->ip += readbyte2(self);
        } break;

        case OP_FJMP: {
            // Reading this regardless of falsiness is required to move to the next
            // non-jump instruction cleanly.
            Word jump = readbyte2(self);
            if (!lua_asboolean(self, -1)) {
                self->cf->ip += jump;
            }
        } break;

        // -*- III:23.3     While Statements ---------------------------------*-
        // Loops are just backwards jumps to the instruction for their condition.
        case OP_LOOP: {
            self->cf->ip -= readbyte2(self); 
        } break;
                        
        case OP_FORPREP: {
            // iterator is lower down the stack, increment is the most recent.
            const TValue *iterator  = lua_poke(self, -3);
            const TValue *condition = lua_poke(self, -2);
            const TValue *increment = lua_poke(self, -1);
            if (!isnumber(iterator)) {
                lua_error(self, "'for' initial value must be a number");
            } else if (!isnumber(condition)) {
                lua_error(self, "'for' limit must be a number");
            } else if (!isnumber(increment)) {
                lua_error(self, "'for' increment must be a number");
            } else if (asnumber(increment) == (lua_Number)0) {
                // Allowed in Lua but I'd prefer to consider it an error
                lua_error(self, "'for' increment must be nonzero");
            }
        } break;

        case OP_FORCOND: {
            // A bit slow to do it like this constantly evaluating increment,
            // but we need this especially for nested loops.
            lua_Number iterator  = asnumber(lua_poke(self, -3));
            lua_Number condition = asnumber(lua_poke(self, -2));
            lua_Number increment = asnumber(lua_poke(self, -1));

            // We push a boolean as an argument for the OP_FJMP instruction.
            if (increment > (lua_Number)0) {
                // keep looping while (iterator <= condition)
                // a.k.a. !(iterator > condition)
                lua_pushboolean(self, !lua_numgt(iterator, condition));
            } else {
                // keep looping while (iterator >= condition)
                // a.k.a. !(iterator < condition)
                lua_pushboolean(self, !lua_numlt(iterator, condition));
            }
        } break;
                         
        case OP_FORINCR: {
            // iterator is lower down the stack, increment is the most recent.
            TValue *iterator  = lua_poke(self, -3);
            TValue *increment = lua_poke(self, -1);
            asnumber(iterator) += asnumber(increment);
        } break;

        // -*- III:24.5.1   Binding arguments to parameters ------------------*-
        case OP_CALL: {
            // We always know that the argument count is written to the constants 
            // array. In practice this should only actually be only 0-255.
            int argc = lua_nextbyte(self);

            // If successful there will be a new frame on the CallFrame stack for
            // the called function. This may also set the prevline counter if we
            // have a Lua function with its own chunk and linerun info. If we
            // have a C function we just call it and leave the prevline as is
            // since we do not change the current frame anyway.
            if (!lua_call(self, argc)) {
                return INTERPRET_RUNTIME_ERROR;
            }

            chunk = &self->cf->function->chunk;
        } break;

        case OP_RETURN: {
            if (lua_return(self)) {
                return INTERPRET_OK;
            }
            // Also set the chunk pointer so we know where to look for constants
            // quickly without doing like 4 dereference operations all the time. 
            chunk = &self->cf->function->chunk;
        } break;
        // I hate how case statements don't introduce their own block scope...
        }
    }
}

InterpretResult interpret_vm(LVM *self, const char *input) {
    // Stack-allocated so we don't have to worry about memory. I need a pointer
    // to this so that its state can be shared across multiple compiler objects.
    LexState lex;
    Compiler compiler;
    compiler.lex = &lex;
    self->input  = input;
    init_compiler(&compiler, NULL, self, FNTYPE_SCRIPT); // NULL = top-level.
    TFunction *script = compile_bytecode(&compiler);
    if (script == NULL) {
        return INTERPRET_COMPILE_ERROR;
    }
    lua_pushfunction(self, script);
    lua_call(self, 0); // Call the implicit main function with no arguments.
    return run_bytecode(self);
}

#undef assert_arithmetic
#undef assert_comparison
#undef binop_template
#undef binop_math
#undef binop_cmp
