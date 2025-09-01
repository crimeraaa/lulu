#include <stdio.h> // sprintf

#include "compiler.hpp"
#include "debug.hpp"
#include "parser.hpp"
#include "vm.hpp"

struct Expr_List {
    Expr last;
    u16  count;
};

static constexpr Expr DEFAULT_EXPR = Expr::make(EXPR_NONE);

static constexpr Token DEFAULT_TOKEN = Token::make(TOKEN_INVALID);

/** @brief Parse expressions via a depth-first-search (DFS).
 *
 * @details
 *  We evaluate the root node and recursively construct a parse tree as long as
 *  there are other nodes of higher precedences. Once there are no more nodes of
 *  higher precedences, the tree is evaluated from the innermost node (i.e. most
 *  recursive) going back to the root node. If there is a remaining node of
 *  lower precedence, we repeat the whole process.
 *
 * @note(2025-08-31)
 *  Forward declaration is entirely for recursive descent parsing.
 */
static Expr
expression(Parser *p, Compiler *c, Precedence limit = PREC_NONE);

static void
chunk(Parser *p, Compiler *c);

// Analogous to `lparser.c:enterblock()` in Lua 5.1.5.
static void
block_push(Compiler *c, Block *b, bool breakable)
{
    lulu_assertf(c->free_reg == small_array_len(c->active),
        "c->free_reg = %i but #c->active = %i",
        c->free_reg, static_cast<int>(small_array_len(c->active)));

    b->prev        = c->block;
    b->break_list  = NO_JUMP;
    b->n_locals    = static_cast<u16>(small_array_len(c->active));
    b->breakable   = breakable;
    b->has_upvalue = false;

    // Chain
    c->block = b;
}

// Analogous to `lparser.c:leaveblock()` in Lua 5.1.5.
static void
block_pop(Compiler *c)
{
    Block *b = c->block;

    // Finalize all the locals' information before popping them.
    int pc = c->pc;
    for (u16 index : slice_from(small_array_slice(c->active), b->n_locals)) {
        c->chunk->locals[index].end_pc = pc;
    }

    // Concept check: tests/function/upvalue6.lua
    if (b->has_upvalue) {
        compiler_code_abc(c, OP_CLOSE, b->n_locals, 0, 0);
    }
    small_array_resize(&c->active, b->n_locals);
    compiler_jump_patch(c, b->break_list);

    // A block only either breaks or controls scope, but never both.
    lulu_assert(!b->breakable || !b->has_upvalue);
    c->free_reg = b->n_locals;
    c->block    = b->prev;
}


/** @brief Checks if we hit a token that 'terminates' a block.
 *
 * @note 2025-07-10
 *  We do not check for correctness, this is just a convenience function acting
 *  as a lookup table. It is the caller's responsibility to consume/expect a
 *  particular token and throw an error if not met.
 */
static bool
block_continue(Parser *p)
{
    switch (p->current.type) {
    case TOKEN_ELSE:
    case TOKEN_ELSEIF:
    case TOKEN_END:
    case TOKEN_EOF:
    case TOKEN_UNTIL:
        return false;
    default:
        break;
    }
    return true;
}

static void
block(Parser *p, Compiler *c)
{
    // Allows OP_CLOSE to be emitted upon exit.
    Block b;
    block_push(c, &b, /*breakable=*/false);
    chunk(p, c);
    // Only blocks with `breakable == true` should have jumps.
    lulu_assert(b.break_list == NO_JUMP);
    block_pop(c);
}

static void
recurse_push(Parser *p, Compiler *c)
{
    compiler_check_limit(c, p->n_calls, PARSER_MAX_RECURSE,
        "recursive C calls");
    p->n_calls++;
}

static void
recurse_pop(Parser *p)
{
    p->n_calls--;
    lulu_assert(p->n_calls >= 0);
}

static Parser
parser_make(lulu_VM *L, OString *source, Stream *z, Builder *b)
{
    Parser p{};
    p.L         = L;
    p.lexer     = lexer_make(L, source, z, b);
    p.current   = DEFAULT_TOKEN;
    p.lookahead = DEFAULT_TOKEN;
    p.builder   = b;
    p.last_line = 1;
    return p;
}

/** @brief Move to the next token unconditionally. */
static void
advance(Parser *p)
{
    // Have a lookahead token to discharge?
    p->last_line = p->lexer.line;
    if (p->lookahead.type != TOKEN_INVALID) {
        p->current   = p->lookahead;
        p->lookahead = DEFAULT_TOKEN;
    } else {
        p->current = lexer_lex(&p->lexer);
    }
}

static Token_Type
lookahead(Parser *p)
{
    // Do not call `lookahead` multiple times in a row.
    lulu_assert(p->lookahead.type == TOKEN_INVALID);
    p->lookahead = lexer_lex(&p->lexer);
    return p->lookahead.type;
}

static bool
check(Parser *p, Token_Type expected)
{
    return p->current.type == expected;
}

static bool
match(Parser *p, Token_Type expected)
{
    bool b = check(p, expected);
    if (b) {
        advance(p);
    }
    return b;
}

[[noreturn]] static void
error_at(Parser *p, Token_Type type, const char *msg)
{
    lexer_error(&p->lexer, type, msg, p->last_line);
}

// Throw an error using the type of the current token.
[[noreturn]] static void
error(Parser *p, const char *msg)
{
    error_at(p, p->current.type, msg);
}

/** @brief Asserts that the current token is type `expected` and advances. */
static void
consume(Parser *p, Token_Type expected)
{
    if (!match(p, expected)) {
        // Worst-case: `"Expected 'function'"`
        char buf[128];
        sprintf(buf, "Expected '%s'", token_cstring(expected));
        error(p, buf);
    }
}

// We assume the Lexer properly marked the interned string somewhere.
static OString *
consume_ident(Parser *p)
{
    Token t = p->current;
    consume(p, TOKEN_IDENT);
    return t.ostring;
}


static void
consume_to_close(Parser *p, Token_Type expected, Token_Type to_close, int line)
{
    if (!match(p, expected)) {
        // Worst-case, even if `sizeof(int) == 8`:
        // `"Expected 'function' (to close 'function' at line
        // 9223372036854775807)"`
        char buf[128];
        sprintf(
            buf,
            "Expected '%s' (to close '%s' at line %i)",
            token_cstring(expected),
            token_cstring(to_close),
            line
        );
        error(p, buf);
    }
}


/** @brief Push comma-separated list of expressions to stack, except last. */
static Expr_List
expression_list(Parser *p, Compiler *c)
{
    Expr e = expression(p, c);
    u16  n = 1;
    while (match(p, TOKEN_COMMA)) {
        compiler_expr_next_reg(c, &e);
        e = expression(p, c);
        n++;
    }
    return {e, n};
}

static void
return_statement(Parser *p, Compiler *c)
{
    u16       ra = c->free_reg;
    Expr_List e{DEFAULT_EXPR, 0};
    if (block_continue(p) && !check(p, TOKEN_SEMI)) {
        e = expression_list(p, c);
        if (e.last.has_multret()) {
            compiler_set_returns(c, &e.last, VARARG);
            ra      = static_cast<u8>(small_array_len(c->active));
            e.count = VARARG;
        } else {
            compiler_expr_next_reg(c, &e.last);
        }
    }
    compiler_code_return(c, ra, e.count);
}

struct Assign {
    Assign *prev;
    Expr    variable;
};

static void
assign_adjust(Compiler *c, u16 n_vars, Expr_List *e)
{
    Expr *last  = &e->last;
    int   extra = static_cast<int>(n_vars) - static_cast<int>(e->count);

    // The last assigning expression has variadic returns?
    if (last->has_multret()) {
        // Include the call itself.
        extra++;
        if (extra < 0) {
            extra = 0;
        }
        compiler_set_returns(c, last, static_cast<u16>(extra));
        if (extra > 1) {
            compiler_reserve_reg(c, extra - 1);
        }
        return;
    }

    // Need to close last expression?
    if (last->type != EXPR_NONE) {
        compiler_expr_next_reg(c, last);
    }

    if (extra > 0) {
        // Register of first uninitialized right-hand side.
        u16 reg = c->free_reg;
        compiler_reserve_reg(c, extra);
        compiler_load_nil(c, reg, extra);
    }
}

static void
assignment(Parser *p, Compiler *c, Assign *last, u16 n_vars, Token *t)
{
    // Check the result of `expression()`.
    if (!last->variable.is_assignable()) {
        error_at(p, t->type, "Expected an assignable expression");
    }

    if (match(p, TOKEN_COMMA)) {
        // Previous one is not needed anymore.
        *t = p->current;
        Assign next{last, expression(p, c)};
        assignment(p, c, &next, n_vars + 1, t);
        return;
    }

    consume(p, TOKEN_ASSIGN);

    Expr_List e    = expression_list(p, c);
    Assign   *iter = last;

    if (n_vars != e.count) {
        assign_adjust(c, n_vars, &e);
        // Reuse the registers occupied by the extra values.
        if (e.count > n_vars) {
            c->free_reg -= e.count - n_vars;
        }
    } else {
        compiler_set_one_return(c, &e.last);
        compiler_set_variable(c, &iter->variable, &e.last);
        iter = iter->prev;
    }

    // Assign from rightmost target going leftmost. Use assigning expressions
    // from right to left as well.
    while (iter != nullptr) {
        Expr tmp = Expr::make_reg(EXPR_DISCHARGED, c->free_reg - 1);
        compiler_set_variable(c, &iter->variable, &tmp);
        iter = iter->prev;
    }
}


static void
local_check_shadowing(Parser *p, Compiler *c, OString *ident)
{
    u16 reg = compiler_get_local(c, c->block->n_locals, ident);
    // Local found and it's not '_'? '_' can be overridden as many times
    // as possible because it helps to toss away function results.
    if (reg != NO_REG && !(ident->len == 1 && ident->data[0] == '_')) {
        char buf[128];
        if (ident->len > 32) {
            sprintf(buf, "Shadowing of local '%.32s...'", ident->to_cstring());
        } else {
            sprintf(buf, "Shadowing of local '%s'", ident->to_cstring());
        }
        error(p, buf);
    }
}

/**
 * @param n
 *      0-based. Which local are we setting the information of? E.g. 0th, 1st,
 *      2nd. This function does NOT update `c->active` in order to prevent
 *      lookup of uninitialized locals.
 */
static void
local_push(Parser *p, Compiler *c, OString *ident, u16 n)
{
    local_check_shadowing(p, c, ident);
    int index = chunk_local_push(p->L, c->chunk, ident);

    // Resulting index wouldn't fit as an element in the active array?
    compiler_check_limit(c, index, MAX_TOTAL_LOCALS, "overall local variables");

    // Pushing to active array would go out of bounds?
    u16 reg = static_cast<u16>(small_array_len(c->active)) + n;
    compiler_check_limit(c, static_cast<int>(reg + 1),
        MAX_ACTIVE_LOCALS, "active local variables");

    u16 *local_index = small_array_get_ptr(&c->active, reg);
    *local_index = static_cast<u16>(index);
    // small_array_push(&c->active, static_cast<u16>(index));
}

static void
local_push_literal(Parser *p, Compiler *c, LString lit, int n)
{
    OString *os = lexer_new_ostring(p->L, &p->lexer, lit);
    local_push(p, c, os, n);
}


/** @brief Create `n` new locals visible to the parser.
 *
 * @note(2025-08-27)
 *      Analogous to `lparser.c:adjustlocalvars()` in Lua 5.1.5.
 */
static void
local_start(Compiler *c, u16 n)
{
    int          pc     = c->pc;
    int          start  = static_cast<int>(small_array_len(c->active));
    Slice<Local> locals = slice(c->chunk->locals);

    // Allow lookup of the (about to be) initialized locals.
    small_array_resize(&c->active, start + n);
    Slice<u16> active = small_array_slice(c->active);
    for (u16 index : slice_from(active, start)) {
        locals[index].start_pc = pc;
    }
}

static void
local_statement(Parser *p, Compiler *c)
{
    u16 n = 0;
    do {
        local_push(p, c, consume_ident(p), n);
        n++;
    } while (match(p, TOKEN_COMMA));
    Expr_List args{DEFAULT_EXPR, 0};
    if (match(p, TOKEN_ASSIGN)) {
        args = expression_list(p, c);
    }

    assign_adjust(c, n, &args);
    local_start(c, n);
}

static int
condition(Parser *p, Compiler *c)
{
    Expr cond = expression(p, c);
    // All 'falses' are equal here.
    if (cond.type == EXPR_NIL) {
        cond.type = EXPR_FALSE;
    }
    compiler_logical_new(c, &cond, true);
    return cond.patch_false;
}

static int
if_condition(Parser *p, Compiler *c)
{
    int pc = condition(p, c);
    consume(p, TOKEN_THEN);
    block(p, c);
    return pc;
}

static void
if_statement(Parser *p, Compiler *c)
{
    int then_jump = if_condition(p, c);
    int else_jump = NO_JUMP;

    while (match(p, TOKEN_ELSEIF)) {
        // All `if` and `elseif` will jump over the same `else` block.
        compiler_jump_add(c, &else_jump, compiler_jump_new(c));

        // A false test must jump to here to try the next `elseif` block.
        compiler_jump_patch(c, then_jump);
        then_jump = if_condition(p, c);
    }

    if (match(p, TOKEN_ELSE)) {
        compiler_jump_add(c, &else_jump, compiler_jump_new(c));
        compiler_jump_patch(c, then_jump);
        block(p, c);
    } else {
        compiler_jump_add(c, &else_jump, then_jump);
    }
    consume(p, TOKEN_END);
    compiler_jump_patch(c, else_jump);
}

static void
while_statement(Parser *p, Compiler *c, int line)
{
    int init_pc = compiler_label_get(c);
    int exit_pc = condition(p, c);
    consume(p, TOKEN_DO);

    // All `break` should go here, not in `block()`.
    Block b;
    block_push(c, &b, /*breakable=*/true);
    block(p, c);

    consume_to_close(p, TOKEN_END, TOKEN_WHILE, line);

    // Goto start whenever we reach here.
    compiler_jump_patch(c, compiler_jump_new(c), init_pc);

    // If condition is falsy, goto here (current pc).
    compiler_jump_patch(c, exit_pc);

    // Resolve breaks only after the unconditional jump was emitted.
    block_pop(c);
}

static void
repeat_statement(Parser *p, Compiler *c, int line)
{
    Block b;
    block_push(c, &b, /*breakable=*/true);

    int body_pc = compiler_label_get(c);
    // A unique property of repeat statements is that their locals are
    // available within the condition statement. So we call chunk()
    // ourselves. This will become a problem when upvalues get involved.
    chunk(p, c);
    consume_to_close(p, TOKEN_UNTIL, TOKEN_REPEAT, line);

    int jump_pc = condition(p, c);
    compiler_jump_patch(c, jump_pc, body_pc);

    block_pop(c);
}

static void
expr_immediate(Parser *p, Compiler *c)
{
    Expr e = expression(p, c);
    compiler_expr_next_reg(c, &e);
}

static void
for_body(Parser *p, Compiler *c, u16 base_reg, u16 n_vars, bool is_numeric)
{
    consume(p, TOKEN_DO);
    local_start(c, 3);

    // Separate scope for user-facing external variables.
    Block b;
    block_push(c, &b, /*breakable=*/false);

    // Control variables already reserved registers via their expressions or
    // but user variables do not yet have registers.
    compiler_reserve_reg(c, n_vars);
    local_start(c, n_vars);

    // goto <loop-pc>
    int prep_pc;
    if (is_numeric) {
        // goto `OP_FOR_LOOP`
        prep_pc = compiler_code_asbx(c, OP_FOR_PREP, base_reg, NO_JUMP);
    } else {
        // goto `OP_FOR_IN_LOOP`
        prep_pc = compiler_jump_new(c);
    }

    block(p, c);
    block_pop(c);
    compiler_jump_patch(c, prep_pc);

    int loop_pc;
    int target;
    if (is_numeric) {
        // can encode jump directly.
        loop_pc = compiler_code_asbx(c, OP_FOR_LOOP, base_reg, NO_JUMP);
        target  = loop_pc;
    } else {
        // can't encode jump directly; use a separate instruction.
        loop_pc = compiler_code_abc(c, OP_FOR_IN, base_reg, 0, n_vars);
        target  = compiler_jump_new(c);
    }
    compiler_jump_patch(c, target, prep_pc + 1);
}


/**
 * @param ident
 *      The variable name of the external (user-facing) iterator variable.
 */
static void
for_numeric(Parser *p, Compiler *c, OString *ident)
{
    u16 index_reg = c->free_reg;
    consume(p, TOKEN_ASSIGN);
    expr_immediate(p, c);

    consume(p, TOKEN_COMMA);
    expr_immediate(p, c);

    if (match(p, TOKEN_COMMA)) {
        expr_immediate(p, c);
    } else {
        Expr incr = Expr::make_number(1);
        compiler_expr_next_reg(c, &incr);
    }
    // The next 3 locals are internal state used by the interpreter; the user
    // has no way of modifying them (save for the potential debug library).
    local_push_literal(p, c, "(for index)"_s, 0);
    local_push_literal(p, c, "(for limit)"_s, 1);
    local_push_literal(p, c, "(for increment)"_s, 2);

    // This is the user-facing (the 'external') index. It mirrors the
    // internal for-index and is implicitly pushed/update as needed.
    local_push(p, c, ident, 3);

    for_body(p, c, index_reg, /* n_vars */ 1, /* is_numeric */ true);
}


/**
 * @brief
 *      `'for' <ident> [ ',' <ident> ]* 'in' <expression> ',' <expression> ','
 * <expression> 'do' <block> 'end'`
 *
 * @param ident
 *      The variable name of the first (and potentially only) loop variable.
 */
static void
for_generic(Parser *p, Compiler *c, OString *ident)
{
    local_push_literal(p, c, "(for generator)"_s, 0);
    local_push_literal(p, c, "(for state)"_s, 1);
    local_push_literal(p, c, "(for control)"_s, 2);

    u16 n_vars = 1;
    local_push(p, c, ident, 3);
    while (match(p, TOKEN_COMMA)) {
        local_push(p, c, consume_ident(p), n_vars + 3);
        n_vars++;
    }
    consume(p, TOKEN_IN);

    u16 gen_reg = c->free_reg;

    /** @brief 3 expressions are needed to keep state.
     *
     * @details
     *  1.) The generator function (local 0)
     *  2.) The state variable (local 1): 1st argument to generator.
     *  3.) The control variable (local 2): 2nd argument to generator.
     */
    Expr_List e = expression_list(p, c);
    assign_adjust(c, 3, &e);
    compiler_check_stack(c, 3);
    for_body(p, c, gen_reg, n_vars, /* is_numeric */ false);
}


/** @brief `'for' <for_init> <for_cond> <for_incr>? 'do' <block> 'end'`
 *
 * @note(2025-07-28)
 *      Numeric `for` can be implemented in terms of existing instructions,
 *      although it could be argued that it is not particularly 'clean'.
 */
static void
for_statement(Parser *p, Compiler *c, int line)
{
    // Scope for loop control variables.
    Block b;
    block_push(c, &b, /*breakable=*/true);

    OString *ident = consume_ident(p);
    switch (p->current.type) {
    // 'for' <ident> '=' <expression> ...
    case TOKEN_ASSIGN:
        for_numeric(p, c, ident);
        break;
    // 'for' <ident> [',' <ident>]* 'in' ...
    case TOKEN_COMMA:
    case TOKEN_IN:
        for_generic(p, c, ident);
        break;
    default:
        error(p, "'=' or 'in' expected");
        break;
    }
    consume_to_close(p, TOKEN_END, TOKEN_FOR, line);
    block_pop(c);
}

static void
break_statement(Parser *p, Compiler *c)
{
    Block *b = c->block;
    bool has_upvalue = false;

    // `if`, `elseif`, `else`, `while`, `for` and `repeat` all make new blocks.
    // But only `for`, `repeat` and `while` are breakable.
    while (b != nullptr && !b->breakable) {
        has_upvalue |= b->has_upvalue;
        b = b->prev;
    }

    if (b == nullptr) {
        error(p, "No block to 'break'");
    }
    // @todo(2025-08-26) Figure out how to get to this point
    if (has_upvalue) {
        compiler_code_abc(c, OP_CLOSE, b->n_locals, 0, 0);
    }
    compiler_jump_add(c, &b->break_list, compiler_jump_new(c));
}

static Compiler
compiler_make(lulu_VM *L, Parser *p, Chunk *f, Table *i, Compiler *prev)
{
    Compiler c{};
    c.L           = L;
    c.prev        = prev;
    c.parser      = p;
    c.chunk       = f;
    c.indexes     = i;
    c.last_target = NO_JUMP;
    return c;
}

static void
function_open(lulu_VM *L, Parser *p, Compiler *c, Compiler *enclosing)
{
    // chunk, table and temporary for garbage collection prevention
    vm_check_stack(L, 3);

    Chunk *chunk = chunk_new(L, p->lexer.source);
    // Push chunk to stack so it is not collected while allocating the table.
    // and so that it is alive throughout the entire compilation.
    vm_push_value(L, Value::make_chunk(chunk));

    Table *t = table_new(L, /*n_hash=*/0, /*n_array=*/0);
    // Ditto.
    vm_push_value(L, Value::make_table(t));

    // Push this compiler to the parser.
    *c = compiler_make(L, p, chunk, t, enclosing);
    p->lexer.indexes = c->indexes;
}

static void
chunk_flatten(lulu_VM *L, Compiler *c, Chunk *p)
{
    dynamic_shrink(L, &p->locals    /*, c->n_locals*/);
    dynamic_shrink(L, &p->upvalues  /*, p->n_upvalues*/);
    dynamic_shrink(L, &p->constants /*, c->n_constants*/);
    dynamic_shrink(L, &p->children  /*, c->n_children*/);
    slice_resize(L, &p->code, c->pc);
    slice_resize(L, &p->lines, c->n_lines);
}

static void
function_close(Parser *p, Compiler *c)
{
    lulu_VM *L = c->L;
    compiler_code_return(c, /*reg=*/0, /*count=*/0);
    // Shrink chunk data to fit.
    Chunk *f = c->chunk;
    chunk_flatten(L, c, f);

#ifdef LULU_DEBUG_PRINT_CODE
    debug_disassemble(f);
#endif // LULU_DEBUG_PRINT_CODE

    vm_pop_value(L);
    vm_pop_value(L);

    // Although chunk and indexes table are not in the stack anymore, they
    // should still not be collected yet. We may need them for a closure.
    gc_mark_compiler_roots(L, c);

    // Pop this compiler from the parser.
    p->lexer.indexes = (c->prev != nullptr) ? c->prev->indexes : nullptr;
}


/**
 * @param type
 *      Determines what bytecode we need to emit when actually retrieving
 *      assigning said upvalue.
 */
static u16
add_upvalue(Compiler *c, u16 index, OString *ident, Expr_Type type)
{
    lulu_VM *L = c->L;
    Chunk   *f = c->chunk;
    int      n = static_cast<int>(f->n_upvalues);

    // If closure references the same upvalue multiple times, reuse it.
    for (int i = 0; i < n; i++) {
        Upvalue_Info up = small_array_get(c->upvalues, i);
        if (up.data == index && up.type == type) {
            return i;
        }
    }

    // Check if new upvalue index would overflow the argument.
    compiler_check_limit(c, n + 1, MAX_UPVALUES, "upvalues");

    // Upvalue does not yet exist; create a new one.
    Upvalue_Info *info = small_array_get_ptr(&c->upvalues, n);
    info->type = type;
    info->data = index;

    // Add this upvalue name for debug purposes.
    return chunk_upvalue_push(L, f, ident);
}

static u16
resolve_local(Compiler *c, OString *ident)
{
    u16 reg = compiler_get_local(c, /*limit=*/0, ident);
    return reg;
}


static void
mark_upvalue(Compiler *c, u16 reg)
{
    Block *b = c->block;
    // Try to find the block that contains 'reg'.
    while (b != nullptr && b->n_locals > reg) {
        b = b->prev;
    }

    if (b != nullptr && b != &c->base_block) {
        b->has_upvalue = true;
    }
}

// Analogous to `lparser.c:singlevaraux()` in Lua 5.1.5.
static u16
resolve_upvalue(Compiler *c, OString *ident)
{
    // No enclosing state to get upvalue of?
    if (c->prev == nullptr) {
        return NO_REG;
    }

    // Base case: upvalue exists in immediately enclosing scope?
    u16 reg = resolve_local(c->prev, ident);
    if (reg != NO_REG) {
        // This specific compiler needs to know some of its locals are being
        // used as upvalues by at least one child.
        mark_upvalue(c->prev, reg);
        return add_upvalue(c, reg, ident, EXPR_LOCAL);
    }

    // Recurse case: upvalue may exist *beyond* immediately enclosing function?
    // Most deeply nested call will return the register or NO_REG.
    reg = resolve_upvalue(c->prev, ident);
    if (reg != NO_REG) {
        // Recursion above would have also marked all intermediate compilers
        // as having upvalues. Concept check: tests/function/upvalue3.lua
        return add_upvalue(c, reg, ident, EXPR_UPVALUE);
    }

    // Upvalue wasn't found?
    return NO_REG;
}

/**
 * @note(2025-08-26)
 *      -   Analogous to `compiler.c:namedVariable()` in Crafting Interpreters,
 *          Chapter 25.2.1: Compiling upvalues.
 */
static Expr
resolve_variable(Compiler *c, OString *ident)
{
    // Resolve from *all* currently active locals.
    u16 reg = resolve_local(c, ident);
    // local with given ident indeed exists?
    if (reg != NO_REG) {
        return Expr::make_reg(EXPR_LOCAL, reg);
    }

    // local with given ident not in current scope; try upvalue.
    u16 up = resolve_upvalue(c, ident);
    if (up != NO_REG) {
        return Expr::make_upvalue(up);
    }

    u32 i = compiler_add_ostring(c, ident);
    return Expr::make_index(EXPR_GLOBAL, i);
}

static void
resolve_field(Parser *p, Compiler *c, Expr *e)
{
    // Table must be in some register, it can be a local.
    compiler_expr_any_reg(c, e);
    u32  i = compiler_add_ostring(c, consume_ident(p));
    Expr k = Expr::make_index(EXPR_CONSTANT, i);
    compiler_get_table(c, e, &k);
}

static Expr
function_var(Parser *p, Compiler *c)
{
    Expr var = resolve_variable(c, consume_ident(p));
    while (match(p, TOKEN_DOT)) {
        resolve_field(p, c, &var);
    }
    return var;
}


// compiler.c:function() in Crafting Interpreters 25.2.2: Flattening upvalues.
static Expr
function_push(Parser *p, Compiler *parent, Compiler *child)
{
    lulu_VM *L = p->L;

    // Child chunk is to be held by the parent.
    chunk_child_push(L, parent->chunk, child->chunk /*, &parent->n_children*/);

    int pc = compiler_code_abx(parent, OP_CLOSURE, NO_REG,
        /*parent->n_children*/ len(parent->chunk->children) - 1);

    for (int i = 0, n = child->chunk->n_upvalues; i < n; i++) {
        Upvalue_Info info = small_array_get(child->upvalues, i);
        OpCode op = (info.type == EXPR_LOCAL) ? OP_MOVE : OP_GET_UPVALUE;
        // Register A is never used here; OP_CLOSURE uses this instruction
        // to set up its upvalues.
        compiler_code_abc(parent, op, 0, info.data, 0);
    }
    return Expr::make_pc(EXPR_RELOCABLE, pc);
}

/**
 * @brief Forms:
 *      1.) 'function' <ident> '(' <ident>* ')' <block> 'end'
 *      2.) 'function' '(' <ident>* ')' <block> 'end'
 */
static Expr
function_definition(Parser *p, Compiler *enclosing, int function_line)
{
    Compiler c;
    function_open(p->L, p, &c, enclosing);

    Chunk *f = c.chunk;
    int paren_line = p->last_line;
    f->line_defined = function_line;
    consume(p, TOKEN_OPEN_PAREN);

    // Prevent segfaults when calling `local_push`.
    block_push(&c, &c.base_block, /*breakable=*/false);
    if (!check(p, TOKEN_CLOSE_PAREN)) {
        u16 n = 0;
        do {
            local_push(p, &c, consume_ident(p), n);
            n++;
        } while (match(p, TOKEN_COMMA));
        local_start(&c, n);
        compiler_reserve_reg(&c, n);
        f->n_params = static_cast<u8>(n);
    }
    consume_to_close(p, TOKEN_CLOSE_PAREN, TOKEN_OPEN_PAREN, paren_line);
    chunk(p, &c);
    block_pop(&c);
    f->last_line_defined = p->lexer.line;
    consume_to_close(p, TOKEN_END, TOKEN_FUNCTION, function_line);
    function_close(p, &c);
    return function_push(p, enclosing, &c);
}

// Form: 'function' <ident>
static void
function_decl(Parser *p, Compiler *c, int function_line)
{
    Expr var  = function_var(p, c);
    Expr body = function_definition(p, c, function_line);
    compiler_set_variable(c, &var, &body);
}

static void
local_function(Parser *p, Compiler *c, int line)
{
    local_push(p, c, consume_ident(p), 0);
    local_start(c, 1);

    Expr var = Expr::make_reg(EXPR_LOCAL, c->free_reg);
    compiler_reserve_reg(c, 1);

    Expr body = function_definition(p, c, line);
    compiler_set_variable(c, &var, &body);
}

static void
declaration(Parser *p, Compiler *c)
{
    Token t    = p->current;
    int   line = p->last_line;
    switch (t.type) {
    case TOKEN_BREAK:
        advance(p); // skip 'break'
        break_statement(p, c);
        break;
    case TOKEN_DO:
        advance(p); // skip `do`
        block(p, c);
        consume_to_close(p, TOKEN_END, TOKEN_DO, line);
        break;
    case TOKEN_FOR:
        advance(p); // skip 'for'
        for_statement(p, c, line);
        break;
    case TOKEN_FUNCTION:
        advance(p); // skip 'function'
        function_decl(p, c, line);
        break;
    case TOKEN_IF:
        advance(p); // skip 'if'
        if_statement(p, c);
        break;
    case TOKEN_LOCAL:
        advance(p); // skip `local`
        if (match(p, TOKEN_FUNCTION)) {
            local_function(p, c, line);
        } else {
            local_statement(p, c);
        }
        break;
    case TOKEN_WHILE:
        advance(p); // skip 'while'
        while_statement(p, c, line);
        break;
    case TOKEN_REPEAT:
        advance(p); // skip 'repeat'
        repeat_statement(p, c, line);
        break;
    case TOKEN_RETURN:
        advance(p); // skip 'return'
        return_statement(p, c);
        break;
    case TOKEN_IDENT: {
        Assign a{nullptr, expression(p, c)};
        // Differentiate `f().field = ...` and `f()`.
        if (a.variable.has_multret()) {
            compiler_set_returns(c, &a.variable, 0);
        } else {
            assignment(p, c, &a, /* n_vars */ 1, &t);
        }
        break;
    }
    default:
        error_at(p, t.type, "Expected an expression");
        break;
    }
    match(p, TOKEN_SEMI);
}

static void
chunk(Parser *p, Compiler *c)
{
    recurse_push(p, c);
    while (block_continue(p)) {
        declaration(p, c);

        /**
         * @note(2025-07-28)
         *      This is VERY important, as it ensures we 'pop' all the
         *      registers that are no longer needed from this point.
         *
         * @details
         *      Concept check:
         * ```lua
         *  local i=0;
         *  while i < 4 do
         *      if (i % 2) == 0 then
         *          local n = i ^ 2
         *          print(n) -- calls `declaration()`, adds a register!
         *          -- because this `if` is calling `block()` which calls
         *          -- `chunk()`, we need to reset the register count.
         *      end
         *  end
         * ```
         */
        c->free_reg = static_cast<u16>(small_array_len(c->active));
    }
    recurse_pop(p);
}

Chunk *
parser_program(lulu_VM *L, OString *source, Stream *z, Builder *b)
{
    Parser   p = parser_make(L, source, z, b);
    Compiler c;
    function_open(L, &p, &c, /*enclosing=*/nullptr);
    // Set up first token
    advance(&p);

    // Helps prevent unnecessarily emitting OP_CLOSE when topmost locals
    // are used as upvalues and are correctly implicitly closed.
    block_push(&c, &c.base_block, /*breakable=*/false);
    chunk(&p, &c);
    block_pop(&c);
    consume(&p, TOKEN_EOF);
    function_close(&p, &c);
    return c.chunk;
}

//=== EXPRESSION PARSING =============================================== {{{

struct Constructor {
    Expr  table; // Information on the OP_NEW_TABLE itself.
    Expr  array_value;
    isize n_hash;
    isize n_array;
    isize to_store;
};

static void
constructor_field(Parser *p, Compiler *c, Constructor *ctor)
{
    u16   reg = c->free_reg;
    Token t   = p->current;
    Expr  k;
    if (match(p, TOKEN_IDENT)) {
        u32 i = compiler_add_ostring(c, t.ostring);
        k     = Expr::make_index(EXPR_CONSTANT, i);
    } else {
        int line = p->last_line;
        consume(p, TOKEN_OPEN_BRACE);
        k = expression(p, c);
        consume_to_close(p, TOKEN_CLOSE_BRACE, TOKEN_OPEN_BRACE, line);
    }

    consume(p, TOKEN_ASSIGN);
    u16 rkb = compiler_expr_rk(c, &k);

    // Don't use `ctor->array_value` because we discharge it later.
    Expr e   = expression(p, c);
    u16  rkc = compiler_expr_rk(c, &e);
    compiler_code_abc(c, OP_SET_TABLE, ctor->table.reg, rkb, rkc);

    // 'pop' whatever registers we used
    c->free_reg = reg;
    ctor->n_hash++;
}

static void
constructor_array(Parser *p, Compiler *c, Constructor *ctor)
{
    ctor->array_value = expression(p, c);
    ctor->n_array++;
    ctor->to_store++;
}

// lparser.c:closelistfield(LexState *ls, ConsControl *cc)
static void
set_array(Compiler *c, Constructor *ctor)
{
    Expr *e = &ctor->array_value;
    // Nothing to do?
    if (e->type == EXPR_NONE) {
        return;
    }

    compiler_expr_next_reg(c, e);
    e->type = EXPR_NONE;
    if (ctor->to_store == FIELDS_PER_FLUSH) {
        compiler_set_array(c, ctor->table.reg, ctor->n_array, ctor->to_store);
        // No more pending array items.
        ctor->to_store = 0;
    }
}

// lparser.c:lastlistfield(FuncState *fs, struct ConsControl *cc)
static void
constructor_array_last(Compiler *c, Constructor *ctor)
{
    // Nothing to do?
    if (ctor->to_store == 0) {
        return;
    }

    Expr *e = &ctor->array_value;
    if (e->has_multret()) {
        compiler_set_returns(c, e, VARARG);
        compiler_set_array(c, ctor->table.reg, ctor->n_array, VARARG);
        // Don't count the function call as it is a variadic return.
        // We will resolve the count at runtime.
        ctor->n_array--;
    } else {
        if (e->type != EXPR_NONE) {
            compiler_expr_next_reg(c, e);
        }
        compiler_set_array(c, ctor->table.reg, ctor->n_array, ctor->to_store);
    }
}

static Expr
constructor(Parser *p, Compiler *c)
{
    Constructor ctor;
    int         pc = compiler_code_abc(c, OP_NEW_TABLE, NO_REG, 0, 0);

    ctor.table       = Expr::make_pc(EXPR_RELOCABLE, pc);
    ctor.array_value = DEFAULT_EXPR;
    ctor.n_hash      = 0;
    ctor.n_array     = 0;
    ctor.to_store    = 0;

    compiler_expr_next_reg(c, &ctor.table);
    while (!check(p, TOKEN_CLOSE_CURLY)) {
        // Discharge any previous array items.
        set_array(c, &ctor);

        // Don't consume yet, `constructor_field()` needs <ident> or '['.
        Token_Type t = p->current.type;
        switch (t) {
        case TOKEN_IDENT: {
            if (lookahead(p) == TOKEN_ASSIGN) {
                constructor_field(p, c, &ctor);
            } else {
                constructor_array(p, c, &ctor);
            }
            break;
        }
        case TOKEN_OPEN_BRACE:
            constructor_field(p, c, &ctor);
            break;
        default:
            constructor_array(p, c, &ctor);
            break;
        }

        // Even if we match one, if '}' follows, the loop ends anyway.
        // E.g. try `t = {x=9, y=10,}`.
        if (!match(p, TOKEN_COMMA)) {
            break;
        }
    }

    consume(p, TOKEN_CLOSE_CURLY);
    constructor_array_last(c, &ctor);

    Instruction *ip = get_code(c, pc);
    ip->set_b(floating_byte_make(ctor.n_hash));
    ip->set_c(floating_byte_make(ctor.n_array));
    return ctor.table;
}

/**
 * @note 2025-06-24
 *  Assumptions:
 *  1.) The caller `e` was pushed to a register.
 *  2.) Our current token is the one right after `(`.
 */
static void
function_call(Parser *p, Compiler *c, Expr *e, int paren_line)
{
    Expr_List args{DEFAULT_EXPR, 0};
    if (!check(p, TOKEN_CLOSE_PAREN)) {
        args = expression_list(p, c);
        compiler_set_returns(c, &args.last, VARARG);
    }
    consume_to_close(p, TOKEN_CLOSE_PAREN, TOKEN_OPEN_PAREN, paren_line);

    lulu_assert(e->type == EXPR_DISCHARGED);
    u16 base = e->reg;
    if (args.last.has_multret()) {
        args.count = VARARG;
    } else {
        // Close last argument.
        if (args.last.type != EXPR_NONE) {
            compiler_expr_next_reg(c, &args.last);
        }
        // g++ warns that `c->free_reg - (base + 1)` converts to `int` in the
        // subtraction.
        args.count = c->free_reg - (base + 1);
    }
    e->type = EXPR_CALL;
    e->pc   = compiler_code_abc(c, OP_CALL, base, args.count, 1);

    // By default, remove the arguments but not the function's register.
    // This allows use to 'reserve' the register.
    c->free_reg = base + 1;
}

static Expr
prefix_expr(Parser *p, Compiler *c)
{
    Token t    = p->current;
    int   line = p->last_line;
    advance(p); // Skip '<number>', '<ident>', '(' or '-'.

    OpCode unary_op;
    switch (t.type) {
    case TOKEN_NIL:
        return Expr::make(EXPR_NIL);
    case TOKEN_TRUE:
        return Expr::make(EXPR_TRUE);
    case TOKEN_FALSE:
        return Expr::make(EXPR_FALSE);
    case TOKEN_FUNCTION:
        return function_definition(p, c, line);
    case TOKEN_NUMBER:
        return Expr::make_number(t.number);
    case TOKEN_STRING: {
        u32 i = compiler_add_ostring(c, t.ostring);
        return Expr::make_index(EXPR_CONSTANT, i);
    }
    case TOKEN_IDENT:
        return resolve_variable(c, t.ostring);
    case TOKEN_OPEN_PAREN: {
        Expr e = expression(p, c);
        consume_to_close(p, TOKEN_CLOSE_PAREN, TOKEN_OPEN_PAREN, line);
        return e;
    }
    case TOKEN_OPEN_CURLY:
        return constructor(p, c);
    case TOKEN_DASH:
        unary_op = OP_UNM;
        goto code_unary;
    case TOKEN_NOT:
        unary_op = OP_NOT;
        goto code_unary;
    case TOKEN_POUND:
        unary_op = OP_LEN;
// Diabolical
code_unary : {
    Expr e = expression(p, c, PREC_UNARY);
    compiler_code_unary(c, unary_op, &e);
    return e;
}
    default:
        error_at(p, t.type, "Expected an expression");
    }
}

static Expr
primary_expr(Parser *p, Compiler *c)
{
    Expr e = prefix_expr(p, c);
    for (;;) {
        int line = p->last_line;
        switch (p->current.type) {
        case TOKEN_OPEN_PAREN: {
            // Function to be called must be on top of the stack.
            compiler_expr_next_reg(c, &e);
            advance(p);
            function_call(p, c, &e, line);
            break;
        }
        case TOKEN_DOT:
            // Skip '.'.
            advance(p);
            resolve_field(p, c, &e);
            break;
        case TOKEN_OPEN_BRACE: {
            // Table must be in some register, it can be a local.
            compiler_expr_any_reg(c, &e);
            advance(p); // Skip '['.
            Expr k = expression(p, c);
            consume_to_close(p, TOKEN_CLOSE_BRACE, TOKEN_OPEN_BRACE, line);
            compiler_get_table(c, &e, &k);
            break;
        }
        default:
            return e;
        }
    }
    return e;
}

enum Binary_Type {
    BINARY_NONE = -1, // Not a valid lookup table key.
    BINARY_AND,
    BINARY_OR, // PREC_AND, PREC_OR
    BINARY_ADD,
    BINARY_SUB, // PREC_TERMINAL
    BINARY_MUL,
    BINARY_DIV,
    BINARY_MOD, // PREC_FACTOR
    BINARY_POW, // PREC_EXPONENT
    BINARY_EQ,
    BINARY_LT,
    BINARY_LEQ, // PREC_COMPARISON, cond=true
    BINARY_NEQ,
    BINARY_GT,
    BINARY_GEQ,    // PREC_COMPARISON, cond=false
    BINARY_CONCAT, // PREC_CONCAT
};

static constexpr int BINARY_TYPE_COUNT = BINARY_CONCAT + 1;

struct Binary_Prec {
    Precedence left, right;
};

static constexpr Binary_Prec
left_assoc(Precedence left_prec)
{
    int  p          = static_cast<int>(left_prec) + 1;
    auto right_prec = static_cast<Precedence>(p);
    return {left_prec, right_prec};
}

static constexpr Binary_Prec
right_assoc(Precedence left_prec)
{
    return {left_prec, left_prec};
}

static const Binary_Prec binary_precs[BINARY_TYPE_COUNT] = {
    /* BINARY_AND */ left_assoc(PREC_AND),
    /* BINARY_OR */ left_assoc(PREC_OR),
    /* BINARY_ADD */ left_assoc(PREC_TERMINAL),
    /* BINARY_SUB */ left_assoc(PREC_TERMINAL),
    /* BINARY_MUL */ left_assoc(PREC_FACTOR),
    /* BINARY_DIV */ left_assoc(PREC_FACTOR),
    /* BINARY_MOD */ left_assoc(PREC_FACTOR),
    /* BINARY_POW */ right_assoc(PREC_EXPONENT),
    /* BINARY_EQ */ left_assoc(PREC_COMPARISON),
    /* BINARY_LT */ left_assoc(PREC_COMPARISON),
    /* BINARY_LEQ */ left_assoc(PREC_COMPARISON),
    /* BINARY_NEQ */ left_assoc(PREC_COMPARISON),
    /* BINARY_GEQ */ left_assoc(PREC_COMPARISON),
    /* BINARY_GT */ left_assoc(PREC_COMPARISON),
    /* BINARY_CONCAT */ right_assoc(PREC_CONCAT),
};

static const OpCode binary_opcodes[BINARY_TYPE_COUNT] = {
    /* BINARY_AND */ OP_TEST,
    /* BINARY_OR */ OP_TEST,
    /* BINARY_ADD */ OP_ADD,
    /* BINARY_SUB */ OP_SUB,
    /* BINARY_MUL */ OP_MUL,
    /* BINARY_DIV */ OP_DIV,
    /* BINARY_MOD */ OP_MOD,
    /* BINARY_POW */ OP_POW,
    /* BINARY_EQ */ OP_EQ,
    /* BINARY_LT */ OP_LT,
    /* BINARY_LEQ */ OP_LEQ,
    /* BINARY_NEQ */ OP_EQ,
    /* BINARY_GEQ */ OP_LEQ,
    /* BINARY_GT */ OP_LT,
    /* BINARY_CONCAT */ OP_CONCAT,
};

static Binary_Type
get_binary(Token_Type type)
{
    switch (type) {
    case TOKEN_AND:
        return BINARY_AND;
    case TOKEN_OR:
        return BINARY_OR;
    case TOKEN_PLUS:
        return BINARY_ADD;
    case TOKEN_DASH:
        return BINARY_SUB;
    case TOKEN_ASTERISK:
        return BINARY_MUL;
    case TOKEN_SLASH:
        return BINARY_DIV;
    case TOKEN_PERCENT:
        return BINARY_MOD;
    case TOKEN_CARET:
        return BINARY_POW;
    case TOKEN_EQ:
        return BINARY_EQ;
    case TOKEN_NOT_EQ:
        return BINARY_NEQ;
    case TOKEN_LESS:
        return BINARY_LT;
    case TOKEN_LESS_EQ:
        return BINARY_LEQ;
    case TOKEN_GREATER:
        return BINARY_GT;
    case TOKEN_GREATER_EQ:
        return BINARY_GEQ;
    case TOKEN_CONCAT:
        return BINARY_CONCAT;
    default:
        break;
    }
    return BINARY_NONE;
}

static void
arith(Parser *p, Compiler *c, Expr *left, Binary_Type b)
{
    // VERY important to call this *before* parsing the right side,
    // if it ends up in a register we want them to be in order.
    if (!left->is_number()) {
        compiler_expr_rk(c, left);
    }
    Expr right = expression(p, c, binary_precs[b].right);
    compiler_code_arith(c, binary_opcodes[b], left, &right);
}

static void
compare(Parser *p, Compiler *c, Expr *left, Binary_Type b, bool cond)
{
    if (!left->is_literal()) {
        compiler_expr_rk(c, left);
    }
    Expr right = expression(p, c, binary_precs[b].right);
    compiler_code_compare(c, binary_opcodes[b], cond, left, &right);
}

static void
logical(Parser *p, Compiler *c, Expr *left, Binary_Type b, bool cond)
{
    compiler_logical_new(c, left, cond);

    Expr right = expression(p, c, binary_precs[b].right);
    compiler_logical_patch(c, left, &right, cond);
}


/**
 * @note 2025-06-14:
 *      Assumes we just consumed the first (prefix) token.
 */
static Expr
expression(Parser *p, Compiler *c, Precedence limit)
{
    recurse_push(p, c);
    Expr left = primary_expr(p, c);
    for (;;) {
        Binary_Type b = get_binary(p->current.type);
        if (b == BINARY_NONE || limit > binary_precs[b].left) {
            break;
        }

        // Skip operator, point to first token of right hand side argument.
        advance(p);

        bool cond = true;
        switch (b) {
        case BINARY_AND:
            logical(p, c, &left, b, true);
            break;
        case BINARY_OR:
            logical(p, c, &left, b, false);
            break;
        case BINARY_ADD:
        case BINARY_SUB:
        case BINARY_MUL:
        case BINARY_DIV:
        case BINARY_MOD:
        case BINARY_POW:
            arith(p, c, &left, b);
            break;
        case BINARY_NEQ:
        case BINARY_GT:
        case BINARY_GEQ:
            cond = false;
            [[fallthrough]];
        case BINARY_EQ:
        case BINARY_LT:
        case BINARY_LEQ:
            compare(p, c, &left, b, cond);
            break;
        case BINARY_CONCAT: {
            // Don't put `left` in an RK register no matter what.
            compiler_expr_next_reg(c, &left);
            Expr right = expression(p, c, binary_precs[b].right);
            compiler_code_concat(c, &left, &right);
            break;
        }
        default:
            lulu_panicf("Invalid Binary_Type(%i)", b);
            break;
        }
    }
    recurse_pop(p);
    return left;
}

//=== }}} ==================================================================
