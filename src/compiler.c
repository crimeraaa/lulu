#include "compiler.h"
#include "parser.h"
#include "string.h"
#include "table.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void luluComp_init(Compiler *comp, Lexer *ls, lulu_VM *vm)
{
    comp->lexer       = ls;
    comp->vm          = vm;
    comp->chunk       = NULL;
    comp->scope_count = 0;
    comp->scope_depth = 0;
    comp->stack_total = 0;
    comp->stack_usage = 0;
    comp->prev_opcode = OP_RETURN;
}

// Will also set `prev_opcode`. Pass `delta` only when `op` has a variable stack
// effect, as indicated by `VAR_DELTA`.
static void adjust_stackinfo(Compiler *comp, OpCode op, int delta)
{
    Lexer  *ls   = comp->lexer;
    OpInfo  info = get_opinfo(op);
    int     push = (info.push == VAR_DELTA) ? delta : info.push;
    int     pop  = (info.pop  == VAR_DELTA) ? delta : info.pop;

    // If both push and pop are VAR_DELTA then something is horribly wrong.
    comp->stack_usage += push - pop;
    comp->prev_opcode = op;
    if (comp->stack_usage > MAX_STACK)
        luluLex_error_consumed(ls, "Function uses too many stack slots");
    if (comp->stack_usage > comp->stack_total)
        comp->stack_total = comp->stack_usage;
}

// EMITTING BYTECODE ------------------------------------------------------ {{{1

static Chunk *current_chunk(Compiler *comp)
{
    return comp->chunk;
}

static void emit_byte(Compiler *comp, Byte data)
{
    Lexer *lexer = comp->lexer;
    int    line  = lexer->consumed.line;
    luluFunc_write_chunk(comp->vm, current_chunk(comp), data, line);
}

void luluComp_emit_opcode(Compiler *comp, OpCode op)
{
    emit_byte(comp, op);
    adjust_stackinfo(comp, op, 0);
}

/**
 * @brief   Optimize consecutive opcodes with a variable delta. Assumes we
 *          already know we want to fold an instruction/set thereof.
 *
 * @note    Assumes that the variable delta is always at the top of the bytecode.
 */
static bool optimized_opcode(Compiler *comp, OpCode op, Byte arg)
{
    Chunk *ck    = current_chunk(comp);
    Byte  *ip    = ck->code + (ck->len - 1);
    int    delta = arg;

    switch (op) {
    // Instead of emitting a separate POP we can just add to the variable delta
    // of SETTABLE.
    case OP_POP:
        if (comp->prev_opcode == OP_SETTABLE)
            return optimized_opcode(comp, OP_SETTABLE, arg);
        break;
    // -1 since each OP_CONCAT assumes at least 2 args, so folding 2+ will
    // result in 1 less explicit pop as it's now been combined.
    case OP_CONCAT:
        delta -= 1;
        break;
    default:
        break;
    }

    // This branch is down here in case of POP needing to optimize SETTABLE.
    if (comp->prev_opcode != op) {
        return false;
    }

    // Adjusting would cause overflow? (Assumes `int` is bigger than `Byte`)
    if (cast(int, *ip) + delta > cast(int, MAX_BYTE)) {
        return false;
    }
    *ip += delta;

    // Do this here mainly for the case of POP delegating to SETTABLE. This is
    // to prevent the wrong prev opcode from being set in such cases.
    adjust_stackinfo(comp, op, delta);
    return true;
}

void luluComp_emit_oparg1(Compiler *comp, OpCode op, Byte arg)
{
    switch (op) {
    case OP_POP:
    case OP_CONCAT: // Fall through
    case OP_NIL:
        if (optimized_opcode(comp, op, arg))
            return;
        // Fall through
    default:
        emit_byte(comp, op);
        emit_byte(comp, arg);
        adjust_stackinfo(comp, op, arg);
        break;
    }
}

void luluComp_emit_oparg2(Compiler *comp, OpCode op, Byte2 arg)
{
    emit_byte(comp, op);
    emit_byte(comp, decode_byte2_msb(arg));
    emit_byte(comp, decode_byte2_lsb(arg));

    switch (op) {
    case OP_SETARRAY:
        // Arg B is the variable delta (how many to pop).
        adjust_stackinfo(comp, op, decode_byte2_lsb(arg));
        break;
    default:
        adjust_stackinfo(comp, op, 0);
        break;
    }
}

void luluComp_emit_oparg3(Compiler *comp, OpCode op, Byte3 arg)
{
    emit_byte(comp, op);
    emit_byte(comp, decode_byte3_msb(arg));
    emit_byte(comp, decode_byte3_mid(arg));
    emit_byte(comp, decode_byte3_lsb(arg));

    switch (op) {
    case OP_SETTABLE:
        // Arg C is the variable delta since it needs to now how many to pop.
        adjust_stackinfo(comp, op, decode_byte3_lsb(arg));
        break;
    default:
        // Other opcodes have a fixed stack effect.
        adjust_stackinfo(comp, op, 0);
        break;
    }
}

void luluComp_emit_identifier(Compiler *comp, const Token *id)
{
    luluComp_emit_oparg3(comp, OP_CONSTANT, luluComp_identifier_constant(comp, id));
}

void luluComp_emit_return(Compiler *comp)
{
    luluComp_emit_opcode(comp, OP_RETURN);
}

int luluComp_emit_table(Compiler *comp)
{
    int offset = current_chunk(comp)->len; // Index about to filled in.
    luluComp_emit_oparg3(comp, OP_NEWTABLE, 0); // By default we assume an empty table.
    return offset;
}

void luluComp_patch_table(Compiler *comp, int offset, Byte3 size)
{
    Byte *ip  = &current_chunk(comp)->code[offset]; // OP_NEWTABLE itself
    *(ip + 1) = decode_byte3_msb(size);
    *(ip + 2) = decode_byte3_mid(size);
    *(ip + 3) = decode_byte3_lsb(size);
}

// 1}}} ------------------------------------------------------------------------

int luluComp_make_constant(Compiler *comp, const Value *vl)
{
    Lexer *ls = comp->lexer;
    int    i  = luluFunc_add_constant(comp->vm, current_chunk(comp), vl);
    if (i + 1 > MAX_CONSTS) {
        luluLex_error_consumed(ls, "Too many constants in current chunk");
    }
    return i;
}

void luluComp_emit_constant(Compiler *comp, const Value *vl)
{
    luluComp_emit_oparg3(comp, OP_CONSTANT, luluComp_make_constant(comp, vl));
}

void luluComp_emit_variable(Compiler *comp, const Token *id)
{
    int  arg     = luluComp_resolve_local(comp, id);
    bool islocal = (arg != -1);

    // Global vs. local operands have different sizes.
    if (islocal) {
        luluComp_emit_oparg1(comp, OP_GETLOCAL, arg);
    } else {
        luluComp_emit_oparg3(comp, OP_GETGLOBAL, luluComp_identifier_constant(comp, id));
    }
}

int luluComp_identifier_constant(Compiler *comp, const Token *id)
{
    Value wrap = make_string(luluStr_copy(comp->vm, id->view));
    return luluComp_make_constant(comp, &wrap);
}

void luluComp_end(Compiler *comp)
{
    luluComp_emit_return(comp);
    if (is_enabled(DEBUG_PRINT_CODE)) {
        printf("[STACK USAGE]:\n"
               "NET:    %i\n"
               "MOST:   %i\n\n",
               comp->stack_usage,
               comp->stack_total);
        luluDbg_disassemble_chunk(current_chunk(comp));
    }
}

void luluComp_begin_scope(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    comp->scope_depth += 1;
    if (comp->scope_depth > MAX_LEVELS) {
        luluLex_error_lookahead(ls, "Function uses too many syntax levels");
    }
}

void luluComp_end_scope(Compiler *comp)
{
    comp->scope_depth--;

    int popped = 0;
    while (comp->scope_count > 0) {
        if (comp->locals[comp->scope_count - 1].depth <= comp->scope_depth)
            break;
        popped++;
        comp->scope_count--;
    }
    // Don't waste 2 bytes if nothing to pop
    if (popped > 0)
        luluComp_emit_oparg1(comp, OP_POP, popped);
}

void luluComp_compile(Compiler *comp, const char *input, Chunk *chunk)
{
    Lexer *ls  = comp->lexer;
    comp->chunk = chunk;
    luluLex_init(ls, input, comp->vm);
    luluLex_next_token(ls);

    luluComp_begin_scope(comp); // Script/REPL can pop its own top-level locals
    while (!luluLex_match_token(ls, TK_EOF)) {
        declaration(comp);
    }
    luluLex_expect_token(ls, TK_EOF, "Expected end of expression");
    luluComp_end_scope(comp);
    luluComp_end(comp);
}

// LOCAL VARIABLES -------------------------------------------------------- {{{1

static bool identifiers_equal(const Token *a, const Token *b)
{
    const StringView *s1 = &a->view;
    const StringView *s2 = &b->view;
    return s1->len == s2->len && cstr_eq(s1->begin, s2->begin, s1->len);
}

int luluComp_resolve_local(Compiler *comp, const Token *id)
{
    for (int i = comp->scope_count - 1; i >= 0; i--) {
        const Local *local = &comp->locals[i];
        // If using itself in initializer, continue to resolve outward.
        if (local->depth != -1 && identifiers_equal(id, &local->ident))
            return i;
    }
    return -1;
}

void luluComp_add_local(Compiler *comp, const Token *id)
{
    if (comp->scope_count + 1 > MAX_LOCALS) {
        luluLex_error_consumed(comp->lexer,
            "More than " stringify(MAX_LOCALS) " local variables reached");
    }
    Local *loc = &comp->locals[comp->scope_count++];
    loc->ident = *id;
    loc->depth = -1;
}

void luluComp_init_local(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    Token *id = &ls->consumed;

    // Detect variable shadowing in the same scope_
    for (int i = comp->scope_count - 1; i >= 0; i--) {
        const Local *loc = &comp->locals[i];
        // Have we hit an outer scope?
        if (loc->depth != -1 && loc->depth < comp->scope_depth) {
            break;
        }
        if (identifiers_equal(id, &loc->ident)) {
            luluLex_error_consumed(ls, "Shadowing of local variable");
        }
    }
    luluComp_add_local(comp, id);
}

void luluComp_define_locals(Compiler *comp, int count)
{
    for (int i = count; i > 0; i--) {
        comp->locals[comp->scope_count - i].depth = comp->scope_depth;
    }
}

// 1}}} ------------------------------------------------------------------------
