#include "compiler.h"
#include "parser.h"
#include "string.h"
#include "table.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void init_compiler(Compiler *cpl, Lexer *ls, lulu_VM *vm)
{
    cpl->lexer       = ls;
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
    if (cpl->stack_usage > MAX_STACK) {
        lexerror_at_consumed(ls, "Function uses too many stack slots");
    }
    if (cpl->stack_usage > cpl->stack_total) {
        cpl->stack_total = cpl->stack_usage;
    }
}

// EMITTING BYTECODE ------------------------------------------------------ {{{1

static Chunk *current_chunk(Compiler *cpl)
{
    return cpl->chunk;
}

static void emit_byte(Compiler *cpl, Byte data)
{
    Alloc *alloc = &cpl->vm->allocator;
    Lexer *lexer = cpl->lexer;
    int    line  = lexer->consumed.line;
    write_chunk(current_chunk(cpl), data, line, alloc);
}

void emit_opcode(Compiler *cpl, OpCode op)
{
    emit_byte(cpl, op);
    adjust_stackinfo(cpl, op, 0);
}

/**
 * @brief   Optimize consecutive opcodes with a variable delta. Assumes we
 *          already know we want to adjust_vardelta an instruction/set thereof.
 *
 * @note    Assumes that the variable delta is always at the top of the bytecode.
 */
static bool adjust_vardelta(Compiler *cpl, OpCode op, Byte arg)
{
    Chunk *ck    = current_chunk(cpl);
    Byte  *ip    = ck->code + (ck->len - 1);
    int    delta = arg;

    switch (op) {
    // Instead of emitting a separate POP we can just add to the variable delta
    // of SETTABLE.
    case OP_POP:
        if (cpl->prev_opcode == OP_SETTABLE) {
            return adjust_vardelta(cpl, OP_SETTABLE, arg);
        }
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
    if (cpl->prev_opcode != op) {
        return false;
    }

    // Adjusting would cause overflow? (Assumes `int` is bigger than `Byte`)
    if (cast(int, *ip) + delta > cast(int, MAX_BYTE)) {
        return false;
    }
    *ip += delta;

    // Do this here mainly for the case of POP delegating to SETTABLE. This is
    // to prevent the wrong prev opcode from being set in such cases.
    adjust_stackinfo(cpl, op, delta);
    return true;
}

void emit_oparg1(Compiler *cpl, OpCode op, Byte arg)
{
    switch (op) {
    case OP_POP:
    case OP_CONCAT: // Fall through
    case OP_NIL:
        if (adjust_vardelta(cpl, op, arg)) {
            return;
        }
        // Fall through
    default:
        emit_byte(cpl, op);
        emit_byte(cpl, arg);
        adjust_stackinfo(cpl, op, arg);
        break;
    }
}

void emit_oparg2(Compiler *cpl, OpCode op, Byte2 arg)
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

void emit_oparg3(Compiler *cpl, OpCode op, Byte3 arg)
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

void emit_identifier(Compiler *cpl, const Token *id)
{
    emit_oparg3(cpl, OP_CONSTANT, identifier_constant(cpl, id));
}

void emit_return(Compiler *cpl)
{
    emit_opcode(cpl, OP_RETURN);
}

int emit_table(Compiler *cpl)
{
    int offset = current_chunk(cpl)->len; // Index about to filled in.
    emit_oparg3(cpl, OP_NEWTABLE, 0); // By default we assume an empty table.
    return offset;
}

void patch_table(Compiler *cpl, int offset, Byte3 size)
{
    Byte *ip  = &current_chunk(cpl)->code[offset]; // OP_NEWTABLE itself
    *(ip + 1) = decode_byte3_msb(size);
    *(ip + 2) = decode_byte3_mid(size);
    *(ip + 3) = decode_byte3_lsb(size);
}

// 1}}} ------------------------------------------------------------------------

int make_constant(Compiler *cpl, const Value *vl)
{
    Alloc *al = &cpl->vm->allocator;
    Lexer *ls = cpl->lexer;
    int    i  = add_constant(current_chunk(cpl), vl, al);
    if (i + 1 > MAX_CONSTS) {
        lexerror_at_consumed(ls, "Too many constants in current chunk");
    }
    return i;
}

void emit_constant(Compiler *cpl, const Value *vl)
{
    emit_oparg3(cpl, OP_CONSTANT, make_constant(cpl, vl));
}

void emit_variable(Compiler *cpl, const Token *id)
{
    int  arg     = resolve_local(cpl, id);
    bool islocal = (arg != -1);

    // Global vs. local operands have different sizes.
    if (islocal) {
        emit_oparg1(cpl, OP_GETLOCAL, arg);
    } else {
        emit_oparg3(cpl, OP_GETGLOBAL, identifier_constant(cpl, id));
    }
}

int identifier_constant(Compiler *cpl, const Token *id)
{
    Value wrap = make_string(copy_string(cpl->vm, id->view));
    return make_constant(cpl, &wrap);
}

void end_compiler(Compiler *cpl)
{
    emit_return(cpl);
    if (is_enabled(DEBUG_PRINT_CODE)) {
        printf("[STACK USAGE]:\n"
               "NET:    %i\n"
               "MOST:   %i\n\n",
               cpl->stack_usage,
               cpl->stack_total);
        disassemble_chunk(current_chunk(cpl));
    }
}

void begin_scope(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    cpl->scope_depth += 1;
    if (cpl->scope_depth > MAX_LEVELS) {
        lexerror_at_lookahead(ls, "Function uses too many syntax levels");
    }
}

void end_scope(Compiler *cpl)
{
    cpl->scope_depth--;

    int popped = 0;
    while (cpl->scope_count > 0) {
        if (cpl->locals[cpl->scope_count - 1].depth <= cpl->scope_depth) {
            break;
        }
        popped++;
        cpl->scope_count--;
    }
    // Don't waste 2 bytes if nothing to pop
    if (popped > 0) {
        emit_oparg1(cpl, OP_POP, popped);
    }
}

void compile(Compiler *cpl, const char *input, Chunk *chunk)
{
    Lexer *ls  = cpl->lexer;
    cpl->chunk = chunk;
    init_lexer(ls, input, cpl->vm);
    next_token(ls);

    begin_scope(cpl); // Script/REPL can pop its own top-level locals
    while (!match_token(ls, TK_EOF)) {
        declaration(cpl);
    }
    expect_token(ls, TK_EOF, "Expected end of expression");
    end_scope(cpl);
    end_compiler(cpl);
}

// LOCAL VARIABLES -------------------------------------------------------- {{{1

static bool identifiers_equal(const Token *a, const Token *b)
{
    const StrView *s1 = &a->view;
    const StrView *s2 = &b->view;
    return s1->len == s2->len && cstr_eq(s1->begin, s2->begin, s1->len);
}

int resolve_local(Compiler *cpl, const Token *id)
{
    for (int i = cpl->scope_count - 1; i >= 0; i--) {
        const Local *local = &cpl->locals[i];
        // If using itself in initializer, continue to resolve outward.
        if (local->depth != -1 && identifiers_equal(id, &local->ident)) {
            return i;
        }
    }
    return -1;
}

void add_local(Compiler *cpl, const Token *id)
{
    if (cpl->scope_count + 1 > MAX_LOCALS) {
        lexerror_at_consumed(cpl->lexer,
            "More than " stringify(MAX_LOCALS) " local variables reached");
    }
    Local *loc = &cpl->locals[cpl->scope_count++];
    loc->ident = *id;
    loc->depth = -1;
}

void init_local(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    Token *id = &ls->consumed;

    // Detect variable shadowing in the same scope_
    for (int i = cpl->scope_count - 1; i >= 0; i--) {
        const Local *loc = &cpl->locals[i];
        // Have we hit an outer scope?
        if (loc->depth != -1 && loc->depth < cpl->scope_depth) {
            break;
        }
        if (identifiers_equal(id, &loc->ident)) {
            lexerror_at_consumed(ls, "Shadowing of local variable");
        }
    }
    add_local(cpl, id);
}

void define_locals(Compiler *cpl, int count)
{
    for (int i = count; i > 0; i--) {
        cpl->locals[cpl->scope_count - i].depth = cpl->scope_depth;
    }
}

// 1}}} ------------------------------------------------------------------------
