#include "compiler.h"
#include "debug.h"
#include "parser.h"
#include "string.h"
#include "table.h"
#include "vm.h"

static void init_scope(Scope *scope)
{
    scope->count = 0;
    scope->depth = 0;
}

void luluCpl_init_compiler(Compiler *cpl, lulu_VM *vm)
{
    init_scope(&cpl->scope);
    cpl->lexer       = nullptr;
    cpl->vm          = vm;
    cpl->chunk       = nullptr;
    cpl->stack_total = 0;
    cpl->stack_usage = 0;
    cpl->prev_opcode = OP_RETURN;
    cpl->can_fold    = true;
}

// Will also set `prev_opcode`. Pass `delta` only when `op` has a variable stack
// effect, as indicated by `VAR_DELTA`.
static void adjust_stackinfo(Compiler *cpl, OpCode op, int delta)
{
    Lexer  *ls   = cpl->lexer;
    OpInfo  info = get_opinfo(op);
    int     push = (info.push == VAR_DELTA) ? delta : info.push;
    int     pop  = (info.pop  == VAR_DELTA) ? delta : info.pop;

    // If both push and pop are VAR_DELTA then something is horribly wrong.
    cpl->stack_usage += push - pop;
    cpl->prev_opcode = op;
    if (cpl->stack_usage > LULU_MAX_STACK)
        luluLex_error_consumed(ls, "Function uses too many stack slots");
    if (cpl->stack_usage > cpl->stack_total)
        cpl->stack_total = cpl->stack_usage;
}

// EMITTING BYTECODE ------------------------------------------------------ {{{1

static Chunk *current_chunk(Compiler *cpl)
{
    return cpl->chunk;
}

static void emit_byte(Compiler *cpl, Byte data)
{
    int line = cpl->lexer->consumed.line;
    luluFun_write_chunk(cpl->vm, current_chunk(cpl), data, line);
}

void luluCpl_emit_opcode(Compiler *cpl, OpCode op)
{
    emit_byte(cpl, op);
    adjust_stackinfo(cpl, op, 0);
}

/**
 * @brief   Optimize consecutive opcodes with a variable delta. Assumes we
 *          already know we want to fold an instruction/set thereof.
 *
 * @note    Assumes that the variable delta is always at the top of the bytecode.
 */
static bool optimized_opcode(Compiler *cpl, OpCode op, Byte arg)
{
    Byte  *ip    = current_chunk(cpl)->code + (current_chunk(cpl)->length - 1);
    int    delta = arg;

    switch (op) {
    case OP_POP:
        // We can condense OP_POP and OP_SETTABLE.
        if (cpl->prev_opcode == OP_SETTABLE)
            return optimized_opcode(cpl, OP_SETTABLE, arg);
        break;
    case OP_CONCAT:
        // Folded OP_CONCAT's require 1 less explicit pop.
        delta -= 1;
        break;
    default:
        break;
    }

    // This branch is down here in case of POP needing to optimize SETTABLE.
    if (cpl->prev_opcode != op)
        return false;

    // Need to NOT optimize just this once?
    if (!cpl->can_fold) {
        cpl->can_fold = true;
        return false;
    }

    // Adjusting would cause overflow?
    if (cast_int(*ip) + delta > cast_int(MAX_BYTE))
        return false;
    *ip += delta;

    // Use unadjusted `arg` for stack info.
    adjust_stackinfo(cpl, op, arg);
    return true;
}

static void emit_oparg1(Compiler *cpl, OpCode op, Byte arg)
{
    emit_byte(cpl, op);
    emit_byte(cpl, arg);
    adjust_stackinfo(cpl, op, arg);
}

void luluCpl_emit_oparg1(Compiler *cpl, OpCode op, Byte arg)
{
    switch (op) {
    case OP_POP:
    case OP_CONCAT: // Fall through
    case OP_NIL:
        if (optimized_opcode(cpl, op, arg))
            return;
        // Fall through
    default:
        emit_oparg1(cpl, op, arg);
        break;
    }
}

void luluCpl_emit_oparg2(Compiler *cpl, OpCode op, Byte2 arg)
{
    emit_byte(cpl, op);
    emit_byte(cpl, decode_byte2_msb(arg));
    emit_byte(cpl, decode_byte2_lsb(arg));

    switch (op) {
    case OP_SETARRAY:
        // Arg B is the variable delta (how many to pop).
        adjust_stackinfo(cpl, op, decode_byte2_lsb(arg));
        break;
    default:
        adjust_stackinfo(cpl, op, 0);
        break;
    }
}

void luluCpl_emit_oparg3(Compiler *cpl, OpCode op, Byte3 arg)
{
    emit_byte(cpl, op);
    emit_byte(cpl, decode_byte3_msb(arg));
    emit_byte(cpl, decode_byte3_mid(arg));
    emit_byte(cpl, decode_byte3_lsb(arg));

    switch (op) {
    case OP_SETTABLE:
        // Arg C is the variable delta since it needs to now how many to pop.
        adjust_stackinfo(cpl, op, decode_byte3_lsb(arg));
        break;
    default:
        // Other opcodes have a fixed stack effect.
        adjust_stackinfo(cpl, op, 0);
        break;
    }
}

Byte3 luluCpl_get_byte3(Compiler *cpl, int offset)
{
    Byte *ip = &current_chunk(cpl)->code[offset];
    return encode_byte3(ip[1], ip[2], ip[3]);
}

int luluCpl_emit_jump(Compiler *cpl)
{
    int offset = current_chunk(cpl)->length;
    // TODO: Use as dummy argument to mark the end of a "patch list".
    luluCpl_emit_oparg3(cpl, OP_JUMP, MAX_BYTE3);
    return offset;
}

int luluCpl_emit_if_jump(Compiler *cpl)
{
    luluCpl_emit_opcode(cpl, OP_TEST);
    return luluCpl_emit_jump(cpl);
}

Byte3 luluCpl_get_jump(Compiler *cpl, int offset, JumpType type)
{
    Byte3 jump = current_chunk(cpl)->length - offset;
    switch (type) {
    case JUMP_FORWARD:
        return jump - get_opsize(OP_JUMP); // skip jump operands as well.
    case JUMP_BACKWARD:
        return jump;
    }
}

void luluCpl_patch_byte3(Compiler *cpl, int offset, Byte3 arg)
{
    Byte *ip = &current_chunk(cpl)->code[offset]; // target opcode itself
    ip[1] = decode_byte3_msb(arg);
    ip[2] = decode_byte3_mid(arg);
    ip[3] = decode_byte3_lsb(arg);
}

void luluCpl_patch_jump(Compiler *cpl, int offset)
{
    Byte3 arg = luluCpl_get_jump(cpl, offset, JUMP_FORWARD);
    if (arg > MAX_SBYTE3)
        luluLex_error_consumed(cpl->lexer, "Too much code to jump over");
    luluCpl_patch_byte3(cpl, offset, arg);
}

int luluCpl_start_loop(Compiler *cpl)
{
    return current_chunk(cpl)->length;
}

void luluCpl_emit_loop(Compiler *cpl, int loop_start)
{
    int   jump = luluCpl_emit_jump(cpl);
    Byte3 arg  = luluCpl_get_jump(cpl, loop_start, JUMP_BACKWARD);
    if (arg > MAX_SBYTE3)
        luluLex_error_consumed(cpl->lexer, "Loop body too large");
    luluCpl_patch_byte3(cpl, jump, arg | MIN_SBYTE3); // toggle sign bit
}

void luluCpl_emit_identifier(Compiler *cpl, String *id)
{
    luluCpl_emit_oparg3(cpl, OP_CONSTANT, luluCpl_identifier_constant(cpl, id));
}

void luluCpl_emit_return(Compiler *cpl)
{
    luluCpl_emit_opcode(cpl, OP_RETURN);
}

int luluCpl_emit_table(Compiler *cpl)
{
    int offset = current_chunk(cpl)->length; // Index about to filled in.
    luluCpl_emit_oparg3(cpl, OP_NEWTABLE, 0); // By default we assume an empty table.
    return offset;
}

void luluCpl_patch_table(Compiler *cpl, int offset, Byte3 size)
{
    luluCpl_patch_byte3(cpl, offset, size);
}

// 1}}} ------------------------------------------------------------------------

void luluCpl_emit_popn(Compiler *cpl, int n)
{
    luluCpl_emit_oparg1(cpl, OP_POP, n);
}

void luluCpl_emit_pop1(Compiler *cpl)
{
    luluCpl_emit_popn(cpl, 1);
}

void luluCpl_pop_cond(Compiler *cpl)
{
    luluCpl_emit_pop1(cpl);
    cpl->stack_usage += 1;
}

void luluCpl_emit_nils(Compiler *cpl, int n)
{
    luluCpl_emit_oparg1(cpl, OP_NIL, n);
}

void luluCpl_adjust_exprlist(Compiler *cpl, int idents, int exprs)
{
    if (exprs == idents)
        return;

    // True: Discard extra expressions. False: Assign nils to remaining idents.
    if (exprs > idents)
        luluCpl_emit_popn(cpl, exprs - idents);
    else
        luluCpl_emit_nils(cpl, idents - exprs);
}

int luluCpl_make_constant(Compiler *cpl, const Value *vl)
{
    Lexer *ls = cpl->lexer;
    int    i  = luluFun_add_constant(cpl->vm, current_chunk(cpl), vl);
    if (i + 1 > LULU_MAX_CONSTS)
        luluLex_error_consumed(ls, stringify(LULU_MAX_CONSTS) "+ constants in current chunk");
    return i;
}

void luluCpl_emit_constant(Compiler *cpl, const Value *vl)
{
    luluCpl_emit_oparg3(cpl, OP_CONSTANT, luluCpl_make_constant(cpl, vl));
}

void luluCpl_emit_variable(Compiler *cpl, String *id)
{
    int  arg     = luluCpl_resolve_local(cpl, id);
    bool islocal = (arg != -1);

    // Global vs. local operands have different sizes.
    if (islocal)
        luluCpl_emit_oparg1(cpl, OP_GETLOCAL, arg);
    else
        luluCpl_emit_oparg3(cpl, OP_GETGLOBAL, luluCpl_identifier_constant(cpl, id));
}

int luluCpl_identifier_constant(Compiler *cpl, String *id)
{
    Value wrap;
    setv_string(&wrap, id);
    return luluCpl_make_constant(cpl, &wrap);
}

void luluCpl_end_compiler(Compiler *cpl)
{
    luluCpl_emit_return(cpl);
    if (IS_DEFINED(LULU_DEBUG_PRINT)) {
        printf("[STACK USAGE]:\n"
               "NET:    %i\n"
               "MOST:   %i\n\n",
               cpl->stack_usage,
               cpl->stack_total);
        luluDbg_disassemble_chunk(current_chunk(cpl));
    }
}

void luluCpl_begin_scope(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    cpl->scope.depth += 1;
    if (cpl->scope.depth > LULU_MAX_LEVELS)
        luluLex_error_lookahead(ls, stringify(LULU_MAX_LEVELS) "+ syntax levels");
}

void luluCpl_end_scope(Compiler *cpl)
{
    Scope *scope  = &cpl->scope;
    int    popped = 0;

    scope->depth -= 1;
    while (scope->count > 0) {
        if (scope->locals[scope->count - 1].depth <= scope->depth)
            break;
        popped += 1;
        scope->count -= 1;
    }
    // Don't waste 2 bytes if nothing to pop
    if (popped > 0)
        luluCpl_emit_popn(cpl, popped);
}

void luluCpl_compile(Compiler *cpl, Lexer *ls, Chunk *chunk)
{
    cpl->lexer = ls;
    cpl->chunk = chunk;
    luluLex_next_token(ls);

    luluCpl_begin_scope(cpl); // Script/REPL can pop its own top-level locals
    while (!luluLex_match_token(ls, TK_EOF)) {
        declaration(cpl, ls);
    }
    luluCpl_end_scope(cpl);
    luluCpl_end_compiler(cpl);
}

// LOCAL VARIABLES -------------------------------------------------------- {{{1

int luluCpl_resolve_local(Compiler *cpl, String *id)
{
    Scope *scope = &cpl->scope;
    for (int i = scope->count - 1; i >= 0; i--) {
        const Local *local = &scope->locals[i];
        // If using itself in initializer, continue to resolve outward.
        if (local->depth != -1 && id == local->identifier)
            return i;
    }
    return -1;
}

// Initializes the current top of the locals array.
static void add_local(Compiler *cpl, String *id)
{
    Scope *scope = &cpl->scope;
    Lexer *ls  = cpl->lexer;
    if (scope->count + 1 > LULU_MAX_LOCALS)
        luluLex_error_consumed(ls, stringify(LULU_MAX_LOCALS) "+ local variables");
    Local *local      = &scope->locals[scope->count++];
    local->identifier = id;
    local->depth      = -1;
}

// Analogous to `declareVariable()` in the book, but only for Lua locals.
// Assumes we just consumed a local variable identifier token.
static void init_local(Compiler *cpl, String *id)
{
    Scope *scope = &cpl->scope;
    Lexer *ls  = cpl->lexer;

    // Detect variable shadowing in the same scope_
    for (int i = scope->count - 1; i >= 0; i--) {
        const Local *local = &scope->locals[i];
        // Have we hit an outer scope?
        if (local->depth != -1 && local->depth < scope->depth)
            break;
        if (id == local->identifier)
            luluLex_error_consumed(ls, "Shadowing of local variable");
    }
    add_local(cpl, id);
}

void luluCpl_define_locals(Compiler *cpl, int count)
{
    Scope *scope = &cpl->scope;
    for (int i = count; i > 0; i--) {
        scope->locals[scope->count - i].depth = scope->depth;
    }
}

void luluCpl_internal_local(Compiler *cpl, const char *name)
{
    String *id = luluStr_copy(cpl->vm, name, strlen(name));
    add_local(cpl, id);
    luluCpl_identifier_constant(cpl, id);
}

void luluCpl_emit_local(Compiler *cpl, String *id)
{
    init_local(cpl, id);
    luluCpl_identifier_constant(cpl, id);
}

// 1}}} ------------------------------------------------------------------------
