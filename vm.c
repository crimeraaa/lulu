#include <time.h>
#include "api.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

/* HELPERS -------------------------------------------------------------- {{{ */

/**
 * Read the current instruction and move the instruction pointer.
 *
 * Remember that postfix increment returns the original value of the expression.
 * So we effectively increment the pointer but we dereference the original one.
 */
static Byte read_byte(CallFrame *self) {
    return *(self->ip++);
}

/**
 * If you have an index greater than 8-bits, calculate that first however you
 * need to then use this macro so you have full control over all side effects.
 */
static TValue read_constant_at(const CallFrame *self, size_t index) {
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

/* }}} */

/* NATIVE FUNCTIONS ----------------------------------------------------- {{{ */

static TValue clock_native(int argc, TValue *bp) {
    (void)argc;
    (void)bp;
    return makenumber((double)clock() / CLOCKS_PER_SEC);
}

static TValue print_native(int argc, TValue *bp) {
    for (int i = 0; i < argc; i++) {
        print_value(bp + i);
        fputc(' ', stdout);
    }
    fputc('\n', stdout);
    return makenil;
}

/* }}} */

/**
 * Make the VM's stack pointer point to the base of the stack array.
 * The same is done for the base pointer.
 */
static inline void reset_stack(LVM *self) {
    self->sp = self->stack;
    self->fc = 0;
}

static CallFrame *current_frame(LVM *self) {
    return &self->frames[self->fc - 1];
}

/**
 * III:18.3.1   Unary negation and runtime errors
 *
 * This function simply prints whatever formatted error message you want.
 * 
 * III:24.5.3   Printing stack traces
 * 
 * We've now added stack traces to help users identify where their program may
 * have gone wrong. It includes a dump of the call stack up until that point.
 */
static void runtime_error(LVM *self, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = self->fc - 1; i >= 0; i--) {
        const LFunction *function = self->frames[i].function;
        const Chunk *chunk        = &function->chunk;
        int line = chunk->lines.runs[chunk->prevline - 1].where;
        fprintf(stderr, "%s:%i: in ", self->name, line);
        if (function->name == NULL) {
            fprintf(stderr, "main chunk\n");
        } else {
            fprintf(stderr, "function '%s'\n", function->name->data);
        }
    }
    reset_stack(self);
}

/**
 * III:24.7     Native Functions
 * 
 * When garbage collection gets involved, it will be important to consider if
 * during the call to `copy_string()` and `new_cfunction` if garbage collection
 * was triggered. If that happens we must tell the GC that we are not actually
 * done with this memory, so storing them on the stack (will) accomplish that
 * when we get to that point.
 */
static void define_nativefn(LVM *self, const char *name, NativeFn func) {
    TString *_name   = copy_string(self, name, strlen(name));
    CFunction *_func = new_cfunction(self, func);
    lua_push(self, makeobject(LUA_TSTRING, _name));
    lua_push(self, makeobject(LUA_TNATIVE, _func));
    table_set(&self->globals, asstring(self->stack[0]), self->stack[1]);
    lua_popn(self, 2);
}

static InterpretResult runtime_arith_error(LVM *self, int offset) {
    const char *typename = lua_typename(self, offset);
    runtime_error(self, "Attempt to perform arithmetic on a %s value", typename);
    return INTERPRET_RUNTIME_ERROR;
}

static InterpretResult runtime_compare_error(LVM *self, int loffset, int roffset) {
    const char *ltypename = lua_typename(self, loffset);
    const char *rtypename = lua_typename(self, roffset);
    runtime_error(self, "Attempt to compare %s with %s", ltypename, rtypename);
    return INTERPRET_RUNTIME_ERROR;
}

static InterpretResult runtime_concat_error(LVM *self, int offset) {
    const char *typename = lua_typename(self, offset);
    runtime_error(self, "Attempt to concatenate a %s value", typename);
    return INTERPRET_RUNTIME_ERROR;
}

void init_vm(LVM *self, const char *name) {
    init_table(&self->globals);
    init_table(&self->strings);
    reset_stack(self);
    self->objects = NULL;
    self->name    = name;
    
    define_nativefn(self, "clock", clock_native);
    define_nativefn(self, "print", print_native);
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
static bool call_function(LVM *self, LFunction *luafn, int argc) {
    if (self->fc >= LUA_MAXFRAMES) {
        runtime_error(self, "Stack overflow.");
        return false;
    }
    if (argc != luafn->arity) {
        runtime_error(self, 
            "Expected %i arguments but got %i.", luafn->arity, argc);
        return false;
    }
    CallFrame *frame = &self->frames[self->fc++];
    frame->function = luafn;
    frame->ip = luafn->chunk.code;    // Beginning of function's bytecode.
    frame->bp = self->sp - argc - 1;  // Base pointer to function object itself.

    // We want to iterate properly over something that has its own chunk, and as
    // a result its own lineruns. We do not do this for C functions as they do
    // not have any chunk, therefore no lineruns info, to begin with.
    frame->function->chunk.prevline = 0;
    return true;
}

/**
 * Calling a C function doesn't involve a lot because we don't create a stack
 * frame or anything, we simply take the arguments, run the function, and push
 * the result. Control is immediately passed back to the caller.
 */
static bool call_cfunction(LVM *self, const CFunction *cfn, int argc) {
    TValue res = cfn->function(argc, self->sp - argc);
    self->sp -= argc + 1; // Point to slot right below the function object.
    lua_push(self, res);
    return true;
}

static bool call_value(LVM *self, CallFrame *frame) {
    // We always know that the argument count is pushed to the top of the stack.
    // In practice this should only actually be only 0-255.
    int argc = read_byte(frame);

    // -1 to poke at top of stack, this is the function object itself.
    // In other words this is the base pointer of the current CallFrame.
    TValue *callee = self->sp - 1 - argc;
    switch (callee->type) {
    case LUA_TFUNCTION: return call_function(self, asfunction(*callee), argc);
    case LUA_TNATIVE:   return call_cfunction(self, ascfunction(*callee), argc);
    default:            break; // Non-callable object type.
    }
    const char *ts = value_typename(callee);
    runtime_error(self, "Attempt to call %s as function", ts);
    return false;
}

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
        lua_push(vm, r);                                                       \
    } while (false)

/**
 * We check lhs and rhs separately so we can report which one is likely to have
 * caused the error.
 */
#define assert_arithmetic(vm)                                                  \
    if (!lua_isnumber(vm, -2)) return runtime_arith_error(vm, -2);             \
    if (!lua_isnumber(vm, -1)) return runtime_arith_error(vm, -1);             \

/**
 * We check both lhs and rhs as you cannot compare non-numbers in terms of
 * greater-then and less-than.
 *
 * This doesn't affect (and should not be used for) equality operators.
 * Meaning comparing equals-to and not-equals-to are valid calls.
 */
#define assert_comparison(vm)                                                  \
    if (!lua_isnumber(vm, -2) || !lua_isnumber(vm, -1)) {                      \
        return runtime_compare_error(vm, -2, -1);                              \
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
static Word read_short(CallFrame *self) {
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
    CallFrame *frame = current_frame(self);

    // Hack, but we reset so we can disassemble properly.
    // We need that start with index 0 into the lines.runs array.
    // So effectively this becames our iterator for each function frame.
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
            lua_push(self, read_constant(frame));
        } break;
        case OP_LCONSTANT: {
            lua_push(self, read_constant_at(frame, read_long(frame)));
        } break;

        // -*- III:18.4     Two New Types ------------------------------------*-
        case OP_NIL:   lua_pushnil(self);            break;
        case OP_TRUE:  lua_pushboolean(self, true);  break;
        case OP_FALSE: lua_pushboolean(self, false); break;

        // -*- III:21.1.2   Expression statements ----------------------------*-
        case OP_POP:   lua_popn(self, 1); break;
        case OP_NPOP: {
            // 1-byte operand is how much to decrement the stack pointer by.
            lua_popn(self, read_byte(frame));
        } break;

        // -*- III:22.4.1   Interpreting local variables ---------------------*-
        case OP_GETLOCAL: {
            Byte slot = read_byte(frame);
            lua_push(self, frame->bp[slot]);
        } break;

        case OP_SETLOCAL: {
            Byte slot = read_byte(frame);
            frame->bp[slot] = *lua_poke(self, -1);
        } break;

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
            lua_push(self, value);
        } break;

        case OP_LGETGLOBAL: {
            TString *name = read_string_at(frame, read_long(frame));
            TValue value;
            if (!table_get(&self->globals, name, &value)) {
                runtime_error(self, "Undefined variable '%s'.", name->data);
                return INTERPRET_RUNTIME_ERROR;
            }
            lua_push(self, value);
        } break;

        // -*- III:21.4     Assignment ---------------------------------------*-
        // Unlike in Lox, Lua allows implicit declaration of globals.
        // Also unlike Lox you simply can't type the equivalent of `var ident;`
        // in Lua as all global variables must be assigned at declaration.
        case OP_SETGLOBAL: {
            TString *name = read_string(frame);
            table_set(&self->globals, name, peekstack(self, -1));
        } break;

        case OP_LSETGLOBAL: {
            TString *name = read_string_at(frame, read_long(frame));
            table_set(&self->globals, name, peekstack(self, -1));
        } break;

        // -*- III:18.4.2   Equality and comparison operators ----------------*-
        case OP_EQ: {
            // Save result before popping so we can push properly.
            // -2 is lhs, -1 is rhs due to the order they were pushed in.
            bool res = lua_equal(self, -2, -1);
            lua_popn(self, 2);
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
            if (!lua_isstring(self, -2)) return runtime_concat_error(self, -2);
            if (!lua_isstring(self, -1)) return runtime_concat_error(self, -1);
            lua_concat(self);
        } break;

        // -*- III:18.4.1   Logical not and falsiness ------------------------*-
        case OP_NOT: {
            *lua_poke(self, -1) = makeboolean(lua_isfalsy(self, -1));
        } break;

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

        // -*- III:23.1     If Statements ------------------------------------*-
        case OP_JMP: {
            Word offset = read_short(frame);
            frame->ip += offset;
        } break;

        case OP_FJMP: {
            Word offset = read_short(frame);
            if (lua_isfalsy(self, -1)) {
                frame->ip += offset;
            }
        } break;
                      
        // -*- III:23.3     While Statements ---------------------------------*-
        case OP_LOOP: {
            Word offset = read_short(frame);
            frame->ip -= offset;
        } break;
                      
        // -*- III:24.5.1   Binging arguments to parameters ------------------*-
        case OP_CALL: {
            // If successful there will be a new frame on the CallFrame stack for
            // the called function. This may also set the prevline counter if we
            // have a Lua function with its own chunk and linerun info. If we
            // have a C function we just call it and leave the prevline as is
            // since we do not change the current frame anyway.
            if (!call_value(self, frame)) {
                return INTERPRET_RUNTIME_ERROR;
            }

            // When we execute the next instruction we want to execute the ones
            // in this CallFrame.
            frame = current_frame(self);
        } break;

        case OP_RETURN: {
            // When a function returns a value, its result will be on the top of
            // the stack. We're about to discard the function's entire stack
            // window so we hold onto the return value.
            TValue res = popstack(self);
            
            // Conceptually discard the call frame. If this was the very last
            // callframe that probably indicates we've finished the top-level.
            self->fc--;
            if (self->fc == 0) {
                lua_popn(self, 1); // Pop the script itself off the VM's stack.
                return INTERPRET_OK;
            }
            // Discard all the slots the callframe was using for its parameters
            // and local variables, which are the same slots the caller (us)
            // used to push the arguments in the first place.
            self->sp = frame->bp;
            lua_push(self, res);
            
            // Return control of the stack back to the caller now that this
            // particular function call is done.
            frame = &self->frames[self->fc - 1];
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
    LFunction *function = compile_bytecode(&compiler);
    if (function == NULL) {
        return INTERPRET_COMPILE_ERROR;
    }
    lua_push(self, makeobject(LUA_TFUNCTION, function));
    call_function(self, function, 0); // Call our implicit `main`, with no arguments.
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
