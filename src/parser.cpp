#include <stdio.h>  // sprintf

#include "parser.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "vm.hpp"

struct LULU_PRIVATE Block {
    Block *prev;        // Stack-allocated linked list.
    isize  break_list;  // Jump list of `break` statements.
    u16    n_locals;    // Number of initialized locals at the time of pushing.
    bool   breakable;   // Is `break` valid for this block?
};

struct LULU_PRIVATE Expr_List {
    Expr last;
    u16  count;
};

static constexpr Expr
DEFAULT_EXPR = Expr::make(EXPR_NONE, /* line */ 0);

static constexpr Token
DEFAULT_TOKEN = Token::make(TOKEN_INVALID, /* line */ 0);

// Forward declaration for recursive descent parsing.
static Expr
expression(Parser *p, Compiler *c, Precedence limit = PREC_NONE);

static void
chunk(Parser *p, Compiler *c);

static void
block_push(Parser *p, Compiler *c, Block *b, bool breakable)
{
    lulu_assert(cast_isize(c->free_reg) == small_array_len(c->active));

    b->prev       = p->block;
    b->break_list = NO_JUMP;
    b->n_locals   = cast(u16)small_array_len(c->active);
    b->breakable  = breakable;

    // Chain
    p->block = b;
}

static void
block_pop(Parser *p, Compiler *c)
{
    Block     *b      = p->block;
    Slice<u16> active = small_array_slice(c->active);

    // Finalize all the locals' information before popping them.
    isize pc = c->pc;
    for (u16 index : slice_from(active, cast_isize(b->n_locals))) {
        c->chunk->locals[index].end_pc = pc;
    }
    small_array_resize(&c->active, b->n_locals);
    compiler_jump_patch(c, b->break_list);
    c->free_reg = b->n_locals;
    p->block    = b->prev;
}


/**
 * @brief
 *  -   Checks if we hit a token that 'terminates' a block.
 *
 * @note 2025-07-10
 *  -   We do not check for correctness, this is just a convenience function
 *      acting as a lookup table.
 *  -   It is the caller's responsibility to consume/expect a particular token
 *      and throw an error if not met.
 */
static bool
block_continue(Parser *p)
{
    switch (p->consumed.type) {
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
    Block b;
    block_push(p, c, &b, /* breakable */ false);
    chunk(p, c);
    // Only blocks with `breakable == true` should have jumps.
    lulu_assert(b.break_list == NO_JUMP);
    block_pop(p, c);
}

static void
recurse_push(Parser *p, Compiler *c)
{
    compiler_check_limit(c, p->n_calls, PARSER_MAX_RECURSE, "recursive C calls");
    p->n_calls++;
}

static void
recurse_pop(Parser *p)
{
    p->n_calls--;
    lulu_assert(p->n_calls >= 0);
}

static Parser
parser_make(lulu_VM *vm, OString *source, const LString &script, Builder *b)
{
    Parser p;
    p.vm        = vm;
    p.lexer     = lexer_make(vm, source, script, b);
    p.consumed  = DEFAULT_TOKEN;
    p.lookahead = DEFAULT_TOKEN;
    p.builder   = b;
    p.block     = nullptr;
    p.n_calls   = 0;
    return p;
}

void
parser_error(Parser *p, const char *msg)
{
    parser_error_at(p, &p->consumed, msg);
}

void
parser_error_at(Parser *p, const Token *t, const char *msg)
{
    LString where = (t->type == TOKEN_EOF) ? token_strings[t->type] : t->lexeme;

    // It is highly important we use a separate string builder from VM, because
    // we don't want it to conflict when writing the formatted string.
    builder_write_lstring(p->vm, p->builder, where);
    const char *s = builder_to_cstring(*p->builder);
    vm_syntax_error(p->vm, p->lexer.source, t->line, "%s at '%s'", msg, s);
}

/**
 * @brief
 *  -   Move to the next token unconditionally.
 */
static void
advance(Parser *p)
{
    // Have a lookahead token to discharge?
    if (p->lookahead.type != TOKEN_INVALID) {
        p->consumed  = p->lookahead;
        p->lookahead = DEFAULT_TOKEN;
    } else {
        p->consumed = lexer_lex(&p->lexer);
    }
}

static Token_Type
lookahead(Parser *p)
{
    // Do not call `lookahead` multiple times in a row.
    lulu_assert(p->lookahead.type == TOKEN_INVALID);
    Token t = lexer_lex(&p->lexer);
    p->lookahead = t;
    return t.type;
}

static bool
check(Parser *p, Token_Type expected)
{
    return p->consumed.type == expected;
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

/**
 * @brief
 *  -   Asserts that the current token is of type `expected` and advances.
 */
static void
consume(Parser *p, Token_Type expected)
{
    if (!match(p, expected)) {
        const char *msg = vm_push_fstring(p->vm, "Expected '%s'",
            raw_data(token_strings[expected]));
        parser_error(p, msg);
    }
}


static void
consume_to_close(Parser *p, Token_Type expected, Token_Type to_close, int line)
{
    if (!match(p, expected)) {
        const char *msg = vm_push_fstring(p->vm,
            "Expected '%s' (to close '%s' at line %i)",
            raw_data(token_strings[expected]), raw_data(token_strings[to_close]),
            line);
        parser_error(p, msg);
    }
}


/**
 * @brief
 *  -   Pushes a comma-separated list of expressions to the stack, except for
 *      the last one.
 *  -   We don't push the last one to allow optimizations.
 */
static Expr_List
expr_list(Parser *p, Compiler *c)
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

static u32
constant_string(Parser *p, Compiler *c, const Token *t)
{
    OString *s = ostring_new(p->vm, t->lexeme);
    u32      i = compiler_add_ostring(c, s);
    return i;
}

static void
return_stmt(Parser *p, Compiler *c, int line)
{
    u16       ra = c->free_reg;
    bool      is_vararg = false;
    Expr_List e{DEFAULT_EXPR, 0};
    if (block_continue(p) && !check(p, TOKEN_SEMI)) {
        e = expr_list(p, c);
        // if (e.last.type == EXPR_CALL) {
        //     compiler_set_returns(c, &e.last, VARARG);
        //     ra        = cast(u8)small_array_len(c->active);
        //     is_vararg = true;
        // } else {
        compiler_expr_next_reg(c, &e.last);
        // }
    }

    compiler_code_return(c, ra, e.count, is_vararg, line);
}

struct LULU_PRIVATE Assign {
    Assign *prev;
    Expr    variable;
};

static void
assign_adjust(Compiler *c, u16 n_vars, Expr_List *e)
{
    int extra = cast_int(n_vars) - cast_int(e->count);
    // The last assigning expression can have variadic returns.
    if (e->last.type == EXPR_CALL) {
        // Include the call itself.
        extra++;
        if (extra < 0) {
            extra = 0;
        }
        compiler_set_returns(c, &e->last, cast(u16)extra);
        if (extra > 1) {
            compiler_reserve_reg(c, cast(u16)(extra - 1));
        }
        return;
    }

    // Need to close last expression?
    if (e->last.type != EXPR_NONE) {
        compiler_expr_next_reg(c, &e->last);
    }

    if (extra > 0) {
        // Register of first uninitialized right-hand side.
        u16 reg = c->free_reg;
        compiler_reserve_reg(c, cast(u16)extra);
        compiler_load_nil(c, reg, extra, e->last.line);
    }
}

static void
assignment(Parser *p, Compiler *c, Assign *last, u16 n_vars, Token *t)
{
    // Check the result of `expression()`.
    if (last->variable.type < EXPR_GLOBAL || last->variable.type > EXPR_INDEXED) {
        parser_error_at(p, t, "Expected an assignable expression");
    }

    if (match(p, TOKEN_COMMA)) {
        // Previous one is not needed anymore.
        *t = p->consumed;
        Assign next{last, expression(p, c)};
        assignment(p, c, &next, n_vars + 1, t);
        return;
    }

    consume(p, TOKEN_ASSIGN);

    Expr_List e    = expr_list(p, c);
    Assign   *iter = last;
    if (n_vars != e.count) {
        assign_adjust(c, n_vars, &e);
        // Reuse the registers occupied by the extra values.
        if (e.count > n_vars) {
            c->free_reg -= e.count - n_vars;
        }
    } else {
        compiler_set_one_return(c, &e.last);
        compiler_set_variable(c, &last->variable, &e.last);
        iter = iter->prev;
    }

    // Assign from rightmost target going leftmost. Use assigning expressions
    // from right to left as well.
    while (iter != nullptr) {
        Expr tmp = Expr::make_reg(EXPR_DISCHARGED, c->free_reg - 1, iter->variable.line);
        compiler_set_variable(c, &iter->variable, &tmp);
        iter = iter->prev;
    }
}

static void
local_push(Parser *p, Compiler *c, const Token *t)
{
    u16 reg;
    OString *id = ostring_new(p->vm, t->lexeme);
    if (compiler_get_local(c, p->block->n_locals, id, &reg)) {
        parser_error_at(p, t, "Shadowing of local variable");
    }
    isize index = chunk_add_local(p->vm, c->chunk, id);

    // Resulting index wouldn't fit as an element in the active array?
    compiler_check_limit(c, index, MAX_TOTAL_LOCALS, "overall local variables",
        t);

    // Pushing to active array would go out of bounds?
    compiler_check_limit(c, small_array_len(c->active) + 1, MAX_ACTIVE_LOCALS,
        "active local variables", t);

    small_array_push(&c->active, cast(u16)index);
}

static void
local_push_literal(Parser *p, Compiler *c, LString lit, int line)
{
    Token tmp = Token::make(TOKEN_IDENTIFIER, line, lit);
    local_push(p, c, &tmp);
}

static void
local_start(Compiler *c, u16 n)
{
    // `lparser.c:adjust_locals()`
    isize pc = c->pc;
    Slice<u16>   active = small_array_slice(c->active);
    Slice<Local> locals = slice(c->chunk->locals);
    for (u16 index : slice_from(active, small_array_len(c->active) - n)) {
        locals[index].start_pc = pc;
    }
}

static void
local_stmt(Parser *p, Compiler *c)
{
    u16 n = 0;
    do {
        Token t = p->consumed;
        consume(p, TOKEN_IDENTIFIER);
        local_push(p, c, &t);
        n++;
    } while (match(p, TOKEN_COMMA));
    // Prevent lookup of uninitialized local variables, e.g. `local x = x`;
    small_array_resize(&c->active, small_array_len(c->active) - cast_isize(n));

    Expr_List args{DEFAULT_EXPR, 0};
    if (match(p, TOKEN_ASSIGN)) {
        args = expr_list(p, c);
    }

    assign_adjust(c, n, &args);

    // Allow lookup of the now-initialized local variables.
    isize start = small_array_len(c->active);
    small_array_resize(&c->active, start + cast_isize(n));
    local_start(c, n);
}

static isize
cond(Parser *p, Compiler *c)
{
    Expr cond = expression(p, c);
    // All 'falses' are equal here.
    if (cond.type == EXPR_NIL) {
        cond.type = EXPR_FALSE;
    }
    compiler_logical_new(c, &cond, true);
    return cond.patch_false;
}

static isize
if_cond(Parser *p, Compiler *c)
{
    isize pc = cond(p, c);
    consume(p, TOKEN_THEN);
    block(p, c);
    return pc;
}

static void
if_stmt(Parser *p, Compiler *c)
{
    isize  then_jump = if_cond(p, c);
    isize  else_jump = NO_JUMP;
    int    line      = p->consumed.line;

    while (match(p, TOKEN_ELSEIF)) {
        // All `if` and `elseif` will jump over the same `else` block.
        compiler_jump_add(c, &else_jump, compiler_jump_new(c, line));

        // A false test must jump to here to try the next `elseif` block.
        compiler_jump_patch(c, then_jump);
        then_jump = if_cond(p, c);
        line = p->consumed.line;
    }

    if (match(p, TOKEN_ELSE)) {
        compiler_jump_add(c, &else_jump, compiler_jump_new(c, line));
        compiler_jump_patch(c, then_jump);
        block(p, c);
    } else {
        compiler_jump_add(c, &else_jump, then_jump);
    }
    consume(p, TOKEN_END);
    compiler_jump_patch(c, else_jump);
}

static void
while_stmt(Parser *p, Compiler *c, int line)
{
    isize init_pc = compiler_label_get(c);
    isize exit_pc = cond(p, c);
    consume(p, TOKEN_DO);

    // All `break` should go here, not in `block()`.
    Block b;
    block_push(p, c, &b, /* breakable */ true);
    block(p, c);

    int end_line = c->parser->consumed.line;
    consume_to_close(p, TOKEN_END, TOKEN_WHILE, line);

    // Goto start whenever we reach here.
    compiler_jump_patch(c, compiler_jump_new(c, end_line), init_pc);

    // If condition is falsy, goto here (current pc).
    compiler_jump_patch(c, exit_pc);

    // Resolve breaks only after the unconditional jump was emitted.
    block_pop(p, c);

}

static void
repeat_stmt(Parser *p, Compiler *c, int line)
{
    Block b;
    block_push(p, c, &b, /* breakable */ true);

    isize body_pc = compiler_label_get(c);
    block(p, c);
    consume_to_close(p, TOKEN_UNTIL, TOKEN_REPEAT, line);

    isize jump_pc = cond(p, c);
    compiler_jump_patch(c, jump_pc, body_pc);

    block_pop(p, c);
}

static Expr
expr_immediate(Parser *p, Compiler *c)
{
    Expr e = expression(p, c);
    compiler_expr_next_reg(c, &e);
    return e;
}

static Expr
for_incr(Parser *p, Compiler *c, int line)
{
    if (match(p, TOKEN_COMMA)) {
        return expr_immediate(p, c);
    }
    Expr incr = Expr::make_number(1, line);
    compiler_expr_next_reg(c, &incr);
    return incr;
}

/**
 * @brief
 *      `'for' <for_init> <for_cond> <for_incr>? 'do' <block> 'end'`
 *
 * @note(2025-07-28)
 *      Numeric `for` can be implemented in terms of existing instructions,
 *      although it could be argued that it is not particularly 'clean'.
 */
static void
for_stmt(Parser *p, Compiler *c, int line)
{
    Block b;
    block_push(p, c, &b, /* breakable */ true);

    Token t = p->consumed;
    consume(p, TOKEN_IDENTIFIER);
    consume(p, TOKEN_ASSIGN);

    Expr index = expr_immediate(p, c);
    consume(p, TOKEN_COMMA);

    Expr limit = expr_immediate(p, c);
    Expr incr = for_incr(p, c, limit.line);
    consume(p, TOKEN_DO);

    // The next 3 locals are internal state used by the interpreter; the user
    // has no way of modifying them (save for the potential debug library).
    local_push_literal(p, c, lstring_literal("(for index)"), index.line);
    local_push_literal(p, c, lstring_literal("(for limit)"), limit.line);
    local_push_literal(p, c, lstring_literal("(for increment)"), incr.line);

    // This is the user-facing (the 'external') index. It mirrors the
    // internal for-index and is implicitly pushed/update as needed.
    local_push(p, c, &t);
    c->free_reg += 1;
    local_start(c, 4);

    isize prep_pc = compiler_code_asbx(c, OP_FOR_PREP, index.reg, NO_JUMP, incr.line);
    block(p, c);

    // Line at the point of 'end' which is a good enough approximation for
    // the increment and unconditional jump.
    int end_line = p->consumed.line;
    consume_to_close(p, TOKEN_END, TOKEN_FOR, line);

    isize loop_pc = compiler_code_asbx(c, OP_FOR_LOOP, index.reg, NO_JUMP, end_line);
    compiler_jump_patch(c, prep_pc, loop_pc);       // goto `OP_FOR_LOOP`
    compiler_jump_patch(c, loop_pc, prep_pc + 1);   // goto <loop-body>
    block_pop(p, c);
}

static void
break_stmt(Parser *p, Compiler *c, int line)
{
    Block *b = p->block;
    while (b != nullptr && !b->breakable) {
        b = b->prev;
    }

    if (b == nullptr) {
        parser_error(p, "No block to 'break'");
    }
    compiler_jump_add(c, &b->break_list, compiler_jump_new(c, line));
}

static void
declaration(Parser *p, Compiler *c)
{
    Token t = p->consumed;
    switch (t.type) {
    case TOKEN_BREAK:
        advance(p); // skip 'break'
        break_stmt(p, c, t.line);
        break;
    case TOKEN_DO:
        advance(p); // skip `do`
        block(p, c);
        consume_to_close(p, TOKEN_END, TOKEN_DO, t.line);
        break;
    case TOKEN_FOR:
        advance(p); // skip 'for'
        for_stmt(p, c, t.line);
        break;
    case TOKEN_IF:
        advance(p); // skip 'if'
        if_stmt(p, c);
        break;
    case TOKEN_LOCAL:
        advance(p); // skip `local`
        local_stmt(p, c);
        break;
    case TOKEN_WHILE:
        advance(p); // skip 'while'
        while_stmt(p, c, t.line);
        break;
    case TOKEN_REPEAT:
        advance(p); // skip 'repeat'
        repeat_stmt(p, c, t.line);
        break;
    case TOKEN_RETURN:
        advance(p); // skip 'return'
        return_stmt(p, c, t.line);
        break;
    case TOKEN_IDENTIFIER: {
        Assign a{nullptr, expression(p, c)};
        // Differentiate `f().field = ...` and `f()`.
        if (a.variable.type == EXPR_CALL) {
            compiler_set_returns(c, &a.variable, 0);
        } else {
            assignment(p, c, &a, /* n_vars */ 1, &t);
        }
        break;
    }
    default:
        parser_error_at(p, &t, "Expected an expression");
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
        c->free_reg = cast(u16)small_array_len(c->active);
    }
    recurse_pop(p);
}

Chunk *
parser_program(lulu_VM *vm, OString *source, const LString &script, Builder *b)
{
    Table *t  = table_new(vm, /* n_hash */ 0, /* n_array */ 0);
    Chunk *ch = chunk_new(vm, source);

    // Push chunk and table to stack so that they are not collected while we
    // are executing.
    vm_push(vm, Value::make_chunk(ch));
    vm_push(vm, Value::make_table(t));

    Parser   p = parser_make(vm, source, script, b);
    Compiler c = compiler_make(vm, &p, ch, t);
    // Set up first token
    advance(&p);
    block(&p, &c);
    consume(&p, TOKEN_EOF);
    compiler_code_return(&c, /* reg */ 0, /* count */ 0, /* is_vararg */ false,
        p.lexer.line);

#ifdef LULU_DEBUG_PRINT_CODE
    debug_disassemble(c.chunk);
#endif // LULU_DEBUG_PRINT_CODE

    vm_pop(vm);
    vm_pop(vm);
    return ch;
}

//=== EXPRESSION PARSING =================================================== {{{

static Expr
resolve_variable(Parser *p, Compiler *c, const Token *t)
{
    Expr e;
    u16 reg;
    OString *id = ostring_new(p->vm, t->lexeme);
    if (compiler_get_local(c, /* limit */ 0, id, &reg)) {
        e = Expr::make_reg(EXPR_LOCAL, reg, t->line);
    } else {
        u32 i = compiler_add_ostring(c, id);
        e = Expr::make_index(EXPR_GLOBAL, i, t->line);
    }
    return e;
}

struct LULU_PRIVATE Constructor {
    Expr table; // Information on the OP_NEW_TABLE itself.
    Expr value;
    int  n_hash;
    int  n_array;
};

static void
ctor_field(Parser *p, Compiler *c, Constructor *ctor)
{
    u16   reg = c->free_reg;
    Token t   = p->consumed;
    Expr k;
    if (match(p, TOKEN_IDENTIFIER)) {
        u32 i = constant_string(p, c, &t);
        k = Expr::make_index(EXPR_CONSTANT, i, t.line);
    } else {
        consume(p, TOKEN_OPEN_BRACE);
        k = expression(p, c);
        consume(p, TOKEN_CLOSE_BRACE);
    }

    consume(p, TOKEN_ASSIGN);
    u16 rkb = compiler_expr_rk(c, &k);

    ctor->value = expression(p, c);
    u16 rkc = compiler_expr_rk(c, &ctor->value);
    compiler_code_abc(c, OP_SET_TABLE, ctor->table.reg, rkb, rkc,
        ctor->value.line);

    // 'pop' whatever registers we used
    c->free_reg = reg;
    ctor->n_hash++;
}

static Expr
constructor(Parser *p, Compiler *c, int line)
{
    Constructor ctor;
    isize pc = compiler_code_abc(c, OP_NEW_TABLE, NO_REG, 0, 0, line);

    ctor.table.type = EXPR_RELOCABLE;
    ctor.table.line = line;
    ctor.table.pc   = pc;
    ctor.value      = DEFAULT_EXPR;
    ctor.n_hash     = 0;
    ctor.n_array    = 0;

    compiler_expr_next_reg(c, &ctor.table);
    while (!check(p, TOKEN_CLOSE_CURLY)) {
        // Don't consume yet, `ctor_field()` needs <identifier> or '['.
        switch (p->consumed.type) {
        case TOKEN_IDENTIFIER: {
            if (lookahead(p) == TOKEN_ASSIGN) {
                ctor_field(p, c, &ctor);
            } else {
                parser_error(p, "Array constructors not yet supported");
            }
            break;
        }
        case TOKEN_OPEN_BRACE:
            ctor_field(p, c, &ctor);
            break;
        default:
            parser_error(p, "Array constructors not yet supported");
            break;
        }

        // Even if we match one, if '}' follows, the loop ends anyway.
        // E.g. try `t = {x=9, y=10,}`.
        if (!match(p, TOKEN_COMMA)) {
            break;
        }
    }

    consume(p, TOKEN_CLOSE_CURLY);

    Instruction *ip = get_code(c, pc);
    ip->set_b(cast(u16)ctor.n_hash);
    ip->set_c(cast(u16)ctor.n_array);
    return ctor.table;
}

/**
 * @note 2025-06-24
 *  Assumptions:
 *  1.) The caller `e` was pushed to a register.
 *  2.) Our current token is the one right after `(`.
 */
static void
function_call(Parser *p, Compiler *c, Expr *e)
{
    Expr_List args{DEFAULT_EXPR, 0};
    if (!check(p, TOKEN_CLOSE_PAREN)) {
        args = expr_list(p, c);
        compiler_set_returns(c, &args.last, VARARG);
    }
    consume(p, TOKEN_CLOSE_PAREN);

    lulu_assert(e->type == EXPR_DISCHARGED);
    u16 base = e->reg;
    if (args.last.type == EXPR_CALL) {
        args.count = VARARG;
    } else {
        // Close last argument.
        if (args.last.type != EXPR_NONE) {
            compiler_expr_next_reg(c, &args.last);
        }
        // g++ warns that `c->free_reg - (base + 1)` converts to `int` in the
        // subtraction.
        args.count = cast(u16)(c->free_reg - (base + 1));
    }
    e->type = EXPR_CALL;
    e->pc   = compiler_code_abc(c, OP_CALL, base, args.count, 0, e->line);

    // By default, remove the arguments but not the function's register.
    // This allows use to 'reserve' the register.
    c->free_reg = base + 1;
}

static Expr
prefix_expr(Parser *p, Compiler *c)
{
    Token t = p->consumed;
    advance(p); // Skip '<number>', '<identifier>', '(' or '-'.

    OpCode unary_op;
    switch (t.type) {
    case TOKEN_NIL:    return Expr::make(EXPR_NIL, t.line);
    case TOKEN_TRUE:   return Expr::make(EXPR_TRUE, t.line);
    case TOKEN_FALSE:  return Expr::make(EXPR_FALSE, t.line);
    case TOKEN_NUMBER: return Expr::make_number(t.number, t.line);
    case TOKEN_STRING: {
        u32 i = compiler_add_ostring(c, t.ostring);
        return Expr::make_index(EXPR_CONSTANT, i, t.line);
    }
    case TOKEN_IDENTIFIER: {
        return resolve_variable(p, c, &t);
    }
    case TOKEN_OPEN_PAREN: {
        Expr e = expression(p, c);
        consume(p, TOKEN_CLOSE_PAREN);
        return e;
    }
    case TOKEN_OPEN_CURLY: {
        return constructor(p, c, t.line);
    }
    case TOKEN_DASH:    unary_op = OP_UNM; goto code_unary;
    case TOKEN_NOT:     unary_op = OP_NOT; goto code_unary;
    case TOKEN_POUND:   unary_op = OP_LEN;
// Diabolical
code_unary: {
        Expr e = expression(p, c, PREC_UNARY);
        compiler_code_unary(c, unary_op, &e);
        return e;
    }
    default:
        parser_error_at(p, &t, "Expected an expression");
    }
}
static Expr
primary_expr(Parser *p, Compiler *c)
{
    Expr e = prefix_expr(p, c);
    for (;;) {
        switch (p->consumed.type) {
        case TOKEN_OPEN_PAREN: {
            // Function to be called must be on top of the stack.
            compiler_expr_next_reg(c, &e);
            advance(p);
            function_call(p, c, &e);
            break;
        }
        case TOKEN_DOT: {
            // Table must in some register.
            compiler_expr_any_reg(c, &e);
            advance(p); // Skip '.'.
            Token t = p->consumed;
            consume(p, TOKEN_IDENTIFIER);

            u32  i = constant_string(p, c, &t);
            Expr k = Expr::make_index(EXPR_CONSTANT, i, t.line);
            compiler_get_table(c, &e, &k);
            break;
        }
        case TOKEN_OPEN_BRACE: {
            // Table must be in some register.
            compiler_expr_any_reg(c, &e);
            advance(p); // Skip '['.
            Expr k = expression(p, c);
            consume(p, TOKEN_CLOSE_BRACE);
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
    BINARY_NONE,                        // PREC_NONE
    BINARY_AND, BINARY_OR,              // PREC_AND, PREC_OR
    BINARY_ADD, BINARY_SUB,             // PREC_TERMINAL
    BINARY_MUL, BINARY_DIV, BINARY_MOD, // PREC_FACTOR
    BINARY_POW,                         // PREC_EXPONENT
    BINARY_EQ,  BINARY_LT, BINARY_LEQ,  // PREC_COMPARISON, cond=true
    BINARY_NEQ, BINARY_GT, BINARY_GEQ,  // PREC_COMPARISON, cond=false
    BINARY_CONCAT,                      // PREC_CONCAT
};

struct Binary_Prec {
    Precedence left, right;
};

static constexpr Binary_Prec
left_assoc(Precedence prec)
{
    return {prec, Precedence(cast_int(prec) + 1)};
}

static constexpr Binary_Prec
right_assoc(Precedence prec)
{
    return {prec, prec};
}

static const Binary_Prec
binary_precs[] = {
    /* BINARY_NONE */   {PREC_NONE, PREC_NONE},
    /* BINARY_AND */    left_assoc(PREC_AND),
    /* BINARY_OR */     left_assoc(PREC_OR),
    /* BINARY_ADD */    left_assoc(PREC_TERMINAL),
    /* BINARY_SUB */    left_assoc(PREC_TERMINAL),
    /* BINARY_MUL */    left_assoc(PREC_FACTOR),
    /* BINARY_DIV */    left_assoc(PREC_FACTOR),
    /* BINARY_MOD */    left_assoc(PREC_FACTOR),
    /* BINARY_POW */    right_assoc(PREC_EXPONENT),
    /* BINARY_EQ */     left_assoc(PREC_COMPARISON),
    /* BINARY_LT */     left_assoc(PREC_COMPARISON),
    /* BINARY_LEQ */    left_assoc(PREC_COMPARISON),
    /* BINARY_NEQ */    left_assoc(PREC_COMPARISON),
    /* BINARY_GEQ */    left_assoc(PREC_COMPARISON),
    /* BINARY_GT */     left_assoc(PREC_COMPARISON),
    /* BINARY_CONCAT */ right_assoc(PREC_CONCAT),
};

static const OpCode
binary_opcodes[] = {
    /* BINARY_NONE */   OP_RETURN,
    /* BINARY_AND */    OP_TEST,
    /* BINARY_OR */     OP_TEST,
    /* BINARY_ADD */    OP_ADD,
    /* BINARY_SUB */    OP_SUB,
    /* BINARY_MUL */    OP_MUL,
    /* BINARY_DIV */    OP_DIV,
    /* BINARY_MOD */    OP_MOD,
    /* BINARY_POW */    OP_POW,
    /* BINARY_EQ */     OP_EQ,
    /* BINARY_LT */     OP_LT,
    /* BINARY_LEQ */    OP_LEQ,
    /* BINARY_NEQ */    OP_EQ,
    /* BINARY_GEQ */    OP_LEQ,
    /* BINARY_GT */     OP_LT,
    /* BINARY_CONCAT */ OP_CONCAT,
};

static Binary_Type
get_binary(Token_Type type)
{
    switch (type) {
    case TOKEN_AND:        return BINARY_AND;
    case TOKEN_OR:         return BINARY_OR;
    case TOKEN_PLUS:       return BINARY_ADD;
    case TOKEN_DASH:       return BINARY_SUB;
    case TOKEN_ASTERISK:   return BINARY_MUL;
    case TOKEN_SLASH:      return BINARY_DIV;
    case TOKEN_PERCENT:    return BINARY_MOD;
    case TOKEN_CARET:      return BINARY_POW;
    case TOKEN_EQ:         return BINARY_EQ;
    case TOKEN_NOT_EQ:     return BINARY_NEQ;
    case TOKEN_LESS:       return BINARY_LT;
    case TOKEN_LESS_EQ:    return BINARY_LEQ;
    case TOKEN_GREATER:    return BINARY_GT;
    case TOKEN_GREATER_EQ: return BINARY_GEQ;
    case TOKEN_CONCAT:     return BINARY_CONCAT;
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
 *  -   Assumes we just consumed the first (prefix) token.
 */
static Expr
expression(Parser *p, Compiler *c, Precedence limit)
{
    recurse_push(p, c);
    Expr left = primary_expr(p, c);
    for (;;) {
        Binary_Type b = get_binary(p->consumed.type);
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
            lulu_unreachable();
            break;
        }

    }
    recurse_pop(p);
    return left;
}

//=== }}} ======================================================================
