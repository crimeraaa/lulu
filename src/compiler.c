#include "compiler.h"
#include "parser.h"
#include "string.h"
#include "table.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void luluCpl_init(Compiler *cpl, lulu_VM *vm)
{
    cpl->lexer       = NULL;
    cpl->vm          = vm;
    cpl->chunk       = NULL;
    cpl->scope_count = 0;
    cpl->scope_depth = 0;
    cpl->stack_total = 0;
    cpl->stack_usage = 0;
    cpl->prev_opcode = OP_RETURN;
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
    if (cpl->stack_usage > MAX_STACK)
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
    Byte  *ip    = current_chunk(cpl)->code + (current_chunk(cpl)->len - 1);
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
    // Adjusting would cause overflow?
    if (cast_int(*ip) + delta > cast_int(MAX_BYTE))
        return false;
    *ip += delta;

    // Use unadjusted `arg` for stack info.
    adjust_stackinfo(cpl, op, arg);
    return true;
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
        emit_byte(cpl, op);
        emit_byte(cpl, arg);
        adjust_stackinfo(cpl, op, arg);
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

int luluCpl_emit_jump(Compiler *cpl)
{
    int offset = current_chunk(cpl)->len;
    luluCpl_emit_oparg3(cpl, OP_JUMP, MAX_BYTE3);
    return offset;
}

void luluCpl_patch_jump(Compiler *cpl, int offset)
{
    size_t arg = current_chunk(cpl)->len - offset - get_opsize(OP_JUMP);
    Byte  *ip  = &current_chunk(cpl)->code[offset]; // jump opcode itself
    if (arg > MAX_BYTE3)
        luluLex_error_consumed(cpl->lexer, "Too much code to jump over");

    ip[1] = decode_byte3_msb(arg);
    ip[2] = decode_byte3_mid(arg);
    ip[3] = decode_byte3_lsb(arg);
}

void luluCpl_emit_identifier(Compiler *cpl, const Token *id)
{
    luluCpl_emit_oparg3(cpl, OP_CONSTANT, luluCpl_identifier_constant(cpl, id));
}

void luluCpl_emit_return(Compiler *cpl)
{
    luluCpl_emit_opcode(cpl, OP_RETURN);
}

int luluCpl_emit_table(Compiler *cpl)
{
    int offset = current_chunk(cpl)->len; // Index about to filled in.
    luluCpl_emit_oparg3(cpl, OP_NEWTABLE, 0); // By default we assume an empty table.
    return offset;
}

void luluCpl_patch_table(Compiler *cpl, int offset, Byte3 size)
{
    Byte *ip  = &current_chunk(cpl)->code[offset]; // OP_NEWTABLE itself
    ip[1] = decode_byte3_msb(size);
    ip[2] = decode_byte3_mid(size);
    ip[3] = decode_byte3_lsb(size);
}

// 1}}} ------------------------------------------------------------------------

int luluCpl_make_constant(Compiler *cpl, const Value *vl)
{
    Lexer *ls = cpl->lexer;
    int    i  = luluFun_add_constant(cpl->vm, current_chunk(cpl), vl);
    if (i + 1 > MAX_CONSTS)
        luluLex_error_consumed(ls, "Too many constants in current chunk");
    return i;
}

void luluCpl_emit_constant(Compiler *cpl, const Value *vl)
{
    luluCpl_emit_oparg3(cpl, OP_CONSTANT, luluCpl_make_constant(cpl, vl));
}

void luluCpl_emit_variable(Compiler *cpl, const Token *id)
{
    int  arg     = luluCpl_resolve_local(cpl, id);
    bool islocal = (arg != -1);

    // Global vs. local operands have different sizes.
    if (islocal)
        luluCpl_emit_oparg1(cpl, OP_GETLOCAL, arg);
    else
        luluCpl_emit_oparg3(cpl, OP_GETGLOBAL, luluCpl_identifier_constant(cpl, id));
}

int luluCpl_identifier_constant(Compiler *cpl, const Token *id)
{
    Value wrap = make_string(luluStr_copy(cpl->vm, id->view));
    return luluCpl_make_constant(cpl, &wrap);
}

void luluCpl_end(Compiler *cpl)
{
    luluCpl_emit_return(cpl);
    if (is_enabled(DEBUG_PRINT_CODE)) {
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
    cpl->scope_depth += 1;
    if (cpl->scope_depth > MAX_LEVELS) {
        luluLex_error_lookahead(ls, "Function uses too many syntax levels");
    }
}

void luluCpl_end_scope(Compiler *cpl)
{
    int popped = 0;

    cpl->scope_depth -= 1;
    while (cpl->scope_count > 0) {
        if (cpl->locals[cpl->scope_count - 1].depth <= cpl->scope_depth)
            break;
        popped += 1;
        cpl->scope_count -= 1;
    }
    // Don't waste 2 bytes if nothing to pop
    if (popped > 0)
        luluCpl_emit_oparg1(cpl, OP_POP, popped);
}

void luluCpl_compile(Compiler *cpl, Lexer *ls, const char *input, Chunk *chunk)
{
    cpl->lexer = ls;
    cpl->chunk = chunk;
    luluLex_init(ls, input, cpl->vm);
    luluLex_next_token(ls);

    luluCpl_begin_scope(cpl); // Script/REPL can pop its own top-level locals
    while (!luluLex_match_token(ls, TK_EOF)) {
        declaration(cpl, ls);
    }
    luluCpl_end_scope(cpl);
    luluCpl_end(cpl);
}

// LOCAL VARIABLES -------------------------------------------------------- {{{1

static bool identifiers_equal(const Token *a, const Token *b)
{
    const StringView *s1 = &a->view;
    const StringView *s2 = &b->view;
    return s1->len == s2->len && cstr_eq(s1->begin, s2->begin, s1->len);
}

int luluCpl_resolve_local(Compiler *cpl, const Token *id)
{
    for (int i = cpl->scope_count - 1; i >= 0; i--) {
        const Local *local = &cpl->locals[i];
        // If using itself in initializer, continue to resolve outward.
        if (local->depth != -1 && identifiers_equal(id, &local->ident))
            return i;
    }
    return -1;
}

void luluCpl_add_local(Compiler *cpl, const Token *id)
{
    Lexer *ls = cpl->lexer;
    if (cpl->scope_count + 1 > MAX_LOCALS)
        luluLex_error_consumed(ls, "More than " stringify(MAX_LOCALS) " local variables");
    Local *loc = &cpl->locals[cpl->scope_count++];
    loc->ident = *id;
    loc->depth = -1;
}

void luluCpl_init_local(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    Token *id = &ls->consumed;

    // Detect variable shadowing in the same scope_
    for (int i = cpl->scope_count - 1; i >= 0; i--) {
        const Local *loc = &cpl->locals[i];
        // Have we hit an outer scope?
        if (loc->depth != -1 && loc->depth < cpl->scope_depth)
            break;
        if (identifiers_equal(id, &loc->ident))
            luluLex_error_consumed(ls, "Shadowing of local variable");
    }
    luluCpl_add_local(cpl, id);
}

void luluCpl_define_locals(Compiler *cpl, int count)
{
    for (int i = count; i > 0; i--) {
        cpl->locals[cpl->scope_count - i].depth = cpl->scope_depth;
    }
}

// 1}}} ------------------------------------------------------------------------
