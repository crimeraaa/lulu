#include <stdio.h>

#include "parser.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "vm.hpp"

struct Block {
    Block *prev;      // Stack-allocated linked list.
    u16    n_locals;  // Number of initialized locals at the time of pushing.
    bool   breakable;
};

struct Expr_List {
    Expr last;
    u16  count;
};

static constexpr Expr
DEFAULT_EXPR = {EXPR_NONE,
    /* line */ 0,
    /* patch_true */ -1,
    /* patch_false */ -1,
    /* <unnamed>::number */ {0}};

static constexpr Token
DEFAULT_TOKEN = {{}, {0}, TOKEN_INVALID, 0};

// Forward declaration for recursive descent parsing.
static Expr
expression(Parser *p, Compiler *c, Precedence limit = PREC_NONE);

static void
declaration(Parser *p, Compiler *c);

static void
block_push(Parser *p, Compiler *c, Block *b, bool breakable)
{
    b->prev      = p->block;
    b->n_locals  = cast(u16)small_array_len(c->active);
    b->breakable = breakable;

    // Chain
    p->block = b;
}

static void
block_pop(Parser *p, Compiler *c)
{
    Block     *b      = p->block;
    Slice<u16> active = small_array_slice(c->active);

    // Finalize all the locals' information before popping them.
    isize pc = len(c->chunk->code);
    for (u16 index : slice_slice(active, cast_isize(b->n_locals), len(active))) {
        c->chunk->locals[index].end_pc = pc;
    }
    small_array_resize(&c->active, b->n_locals);
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
        return false;
    default:
        break;
    }
    return true;
}

static void
block(Parser *p, Compiler *c, bool breakable = false)
{
    Block b;
    block_push(p, c, &b, breakable);
    while (block_continue(p)) {
        declaration(p, c);
    }
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

Parser
parser_make(lulu_VM *vm, LString source, LString script, Builder *b)
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
    const char *s = builder_to_cstring(p->builder);
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
        // Assume our longest token is '<identifier>'.
        char buf[64];
        sprintf(buf, "Expected '%s'", raw_data(token_strings[expected]));
        parser_error(p, buf);
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

static Expr
resolve_variable(Parser *p, Compiler *c, const Token *t)
{
    Expr e;
    u16 reg;
    OString *id = ostring_new(p->vm, t->lexeme);
    if (compiler_get_local(c, /* limit */ 0, id, &reg)) {
        e = expr_make_reg(EXPR_LOCAL, reg, t->line);
    } else {
        u32 i = compiler_add_ostring(c, id);
        e = expr_make_index(EXPR_GLOBAL, i, t->line);
    }
    return e;
}

struct Constructor {
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
        k = expr_make_index(EXPR_CONSTANT, i, t.line);
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

static Expr
prefix_expr(Parser *p, Compiler *c)
{
    Token t = p->consumed;
    advance(p); // Skip '<number>', '<identifier>', '(' or '-'.
    switch (t.type) {
    case TOKEN_NIL:    return expr_make(EXPR_NIL, t.line);
    case TOKEN_TRUE:   return expr_make(EXPR_TRUE, t.line);
    case TOKEN_FALSE:  return expr_make(EXPR_FALSE, t.line);
    case TOKEN_NUMBER: return expr_make_number(t.number, t.line);
    case TOKEN_STRING: {
        u32 i = compiler_add_ostring(c, t.ostring);
        return expr_make_index(EXPR_CONSTANT, i, t.line);
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
    case TOKEN_DASH: {
        Expr e = expression(p, c, PREC_UNARY);
        compiler_code_unary(c, OP_UNM, &e);
        return e;
    }
    case TOKEN_NOT: {
        Expr e = expression(p, c, PREC_UNARY);
        compiler_code_unary(c, OP_NOT, &e);
        return e;
    }
    default:
        parser_error_at(p, &t, "Expected an expression");
    }
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
        args.count = c->free_reg - (base + 1);
    }
    e->type = EXPR_CALL;
    e->pc   = compiler_code_abc(c, OP_CALL, base, args.count, 0, e->line);

    // By default, remove the arguments but not the function's register.
    // This allows use to 'reserve' the register.
    c->free_reg = base + 1;
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
            Expr k = expr_make_index(EXPR_CONSTANT, i, t.line);
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

/**
 * @note 2025-06-16:
 *  -   `OP_RETURN` is our 'invalid' binary opcode.
 */
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
    if (!expr_is_number(left)) {
        compiler_expr_rk(c, left);
    }
    Expr right = expression(p, c, binary_precs[b].right);
    compiler_code_arith(c, binary_opcodes[b], left, &right);
}

static void
compare(Parser *p, Compiler *c, Expr *left, Binary_Type b, bool cond)
{
    if (!expr_is_literal(left)) {
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

        // Skip operator, point to first token of right hand side argument->
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
        case BINARY_NEQ: // fall-through
        case BINARY_GT:  // fall-through
        case BINARY_GEQ: cond = false;
        case BINARY_EQ:  // fall-through
        case BINARY_LT:  // fall-through
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
            lulu_assertf(false, "Invalid Binary_Type(%i)", b);
            lulu_unreachable();
            break;
        }

    }
    recurse_pop(p);
    return left;
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

struct Assign {
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
        Expr tmp = expr_make_reg(EXPR_DISCHARGED, c->free_reg - 1, iter->variable.line);
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
local_start(Compiler *c, u16 n)
{
    // `lparser.c:adjust_locals()`
    isize  pc    = len(c->chunk->code);
    isize  start = small_array_len(c->active);

    small_array_resize(&c->active, start + cast_isize(n));
    Slice<u16>   active = small_array_slice(c->active);
    Slice<Local> locals = slice_slice(c->chunk->locals);
    for (u16 index : slice_slice(active, start, len(active))) {
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
    local_start(c, n);
}

static void
do_block(Parser *p, Compiler *c)
{
    block(p, c);
    consume(p, TOKEN_END);
}

static Expr
if_cond(Parser *p, Compiler *c)
{
    Expr cond = expression(p, c);
    consume(p, TOKEN_THEN);
    compiler_logical_new(c, &cond, true);
    block(p, c);
    return cond;
}

static void
if_stmt(Parser *p, Compiler *c)
{
    Expr   cond      = if_cond(p, c);
    isize  else_jump = NO_JUMP;
    int    line      = p->consumed.line;

    while (match(p, TOKEN_ELSEIF)) {
        // All `if` and `elseif` will jump over the same `else` block.
        compiler_jump_add(c, &else_jump, compiler_jump_new(c, line));

        // A false test must jump to here to try the next `elseif` block.
        compiler_jump_patch(c, cond.patch_false);
        cond = if_cond(p, c);
        line = p->consumed.line;
    }

    if (match(p, TOKEN_ELSE)) {
        compiler_jump_add(c, &else_jump, compiler_jump_new(c, line));
        compiler_jump_patch(c, cond.patch_false);
        block(p, c);
    } else {
        compiler_jump_add(c, &else_jump, cond.patch_false);
    }
    consume(p, TOKEN_END);
    compiler_jump_patch(c, else_jump);
}

static void
declaration(Parser *p, Compiler *c)
{
    recurse_push(p, c);
    Token t = p->consumed;
    switch (t.type) {
    case TOKEN_DO: {
        advance(p); // skip `do`
        do_block(p, c);
        break;
    }
    case TOKEN_IF: {
        advance(p); // skip 'if'
        if_stmt(p, c);
        break;
    }
    case TOKEN_LOCAL:
        advance(p); // skip `local`
        local_stmt(p, c);
        break;
    case TOKEN_RETURN:
        advance(p);
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
    recurse_pop(p);
    match(p, TOKEN_SEMI);
}

Chunk *
parser_program(lulu_VM *vm, LString source, LString script, Builder *b)
{
    Table *t  = table_new(vm, /* n_hash */ 0, /* n_array */ 0);
    Chunk *ch = chunk_new(vm, source, t);

    // Push chunk and table to stack so that they are not collected while we
    // are executing.
    vm_push(vm, value_make_chunk(ch));
    vm_push(vm, value_make_table(t));

    Parser   p = parser_make(vm, source, script, b);
    Compiler c = compiler_make(vm, &p, ch);
    // Set up first token
    advance(&p);

    Block bl;
    block_push(&p, &c, &bl, /* breakable */ false);
    while (block_continue(&p)) {
        declaration(&p, &c);
        c.free_reg = cast(u16)small_array_len(c.active);
    }
    block_pop(&p, &c);

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
