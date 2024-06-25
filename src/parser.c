#include "parser.h"

#include "object.h"
#include "string.h"
#include "table.h"
#include "vm.h"

// Forward declarations to allow recursive descent parsing.
static ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *cpl, Lexer *ls, Precedence prec);

/**
 * @brief   In Lua, globals aren't declared, but rather assigned as needed.
 *          This may be inconsistent with the design that accessing undefined
 *          globals is an error, but at the same time I dislike implicit nil for
 *          undefined globals.
 *
 * @details statement ::= identlist '=' exprlist
 *                      | 'do' block 'end'
 *                      | 'print' expression
 *                      | expression
 *          identlist ::= identifier [',' identifier]*
 *          exprlist  ::= expression [',' expression]*
 *
 * @note    By themselves, statements should have zero net stack effect.
 */
static void statement(Compiler *cpl, Lexer *ls);

/**
 * @brief   By itself, always results in exactly 1 value being pushed.
 *
 * @note    We don't parse the same precedence as assignment in order to
 *          disallow C-style constructs like `print(x = 13)`, which is usually
 *          intended to be `print(x == 13)`.
 */
static void expression(Compiler *cpl, Lexer *ls);

static int parse_exprlist(Compiler *cpl, Lexer *ls)
{
    int exprs = 0;
    do {
        expression(cpl, ls);
        exprs += 1;
    } while (luluLex_match_token(ls, TK_COMMA));
    return exprs;
}

static void adjust_exprlist(Compiler *cpl, int idents, int exprs)
{
    if (exprs == idents)
        return;

    // True: Discard extra expressions. False: Assign nils to remaining idents.
    if (exprs > idents)
        luluCpl_emit_oparg1(cpl, OP_POP, exprs - idents);
    else
        luluCpl_emit_oparg1(cpl, OP_NIL, idents - exprs);
}

// EXPRESSIONS ------------------------------------------------------------ {{{1

// INFIX ------------------------------------------------------------------ {{{2

static OpCode get_binop(TkType type)
{
    switch (type) {
    case TK_PLUS:    return OP_ADD;
    case TK_DASH:    return OP_SUB;
    case TK_STAR:    return OP_MUL;
    case TK_SLASH:   return OP_DIV;
    case TK_PERCENT: return OP_MOD;
    case TK_CARET:   return OP_POW;

    case TK_EQ:      return OP_EQ;
    case TK_LT:      return OP_LT;
    case TK_LE:      return OP_LE;
    default:
        // Should not happen
        return OP_RETURN;
    }
}

// Assumes we just consumed a binary operator as a possible infix expression,
// and that the left-hand side has been fully compile.
static void binary(Compiler *cpl, Lexer *ls)
{
    TkType type  = ls->consumed.type;
    int    assoc = (type != TK_CARET); // True: right associative. False: left.

    parse_precedence(cpl, ls, get_parserule(type)->precedence + assoc);

    // NEQ, GT and GE must be encoded as logical NOT of their counterparts.
    switch (type) {
    case TK_NEQ:
        luluCpl_emit_opcode(cpl, OP_EQ);
        luluCpl_emit_opcode(cpl, OP_NOT);
        break;
    case TK_GT:
        luluCpl_emit_opcode(cpl, OP_LE);
        luluCpl_emit_opcode(cpl, OP_NOT);
        break;
    case TK_GE:
        luluCpl_emit_opcode(cpl, OP_LT);
        luluCpl_emit_opcode(cpl, OP_NOT);
        break;
    default:
        luluCpl_emit_opcode(cpl, get_binop(type));
        break;
    }
}

/**
 * @brief   Assumes we just consumed a `..` token and that the first argument
 *          has already been compiled.
 *
 * @note    Although right associative, we don't recursively compile concat
 *          expressions in the same grouping.
 *
 *          We do this iteratively which is (slightly) better than constantly
 *          allocating stack frames for recursive calls.
 */
static void concat(Compiler *cpl, Lexer *ls)
{
    int argc = 1;
    do {
        if (argc + 1 > MAX_BYTE)
            luluLex_error_consumed(ls, "Too many consecutive concatenations");
        parse_precedence(cpl, ls, PREC_CONCAT + 1);
        argc += 1;
    } while (luluLex_match_token(ls, TK_CONCAT));
    luluCpl_emit_oparg1(cpl, OP_CONCAT, argc);
}

static void index_(Compiler *cpl, Lexer *ls, bool can_assign)
{
    expression(cpl, ls);
    luluLex_expect_token(ls, TK_RBRACKET, "to close '['");
    if (!can_assign)
        luluCpl_emit_opcode(cpl, OP_GETTABLE);
}

static void field_(Compiler *cpl, Lexer *ls, bool can_assign)
{
    luluLex_expect_token(ls, TK_IDENT, "after '.'");
    luluCpl_emit_identifier(cpl, ls->consumed.data.string);
    if (!can_assign)
        luluCpl_emit_opcode(cpl, OP_GETTABLE);
}

// <identifier> '[' <expression> ']'
static void index(Compiler *cpl, Lexer *ls)
{
    index_(cpl, ls, !CAN_ASSIGN);
}

// <identifier> '.' <identifier>
static void field(Compiler *cpl, Lexer *ls)
{
    field_(cpl, ls, !CAN_ASSIGN);
}

// Assumes left-hand side has already been compiled.
static void logic_and(Compiler *cpl, Lexer *ls)
{
    // If LHS is truthy, skip the jump. If falsy, pop LHS then push RHS.
    int end_jump = luluCpl_emit_if_jump(cpl, CAN_POP);
    end_jump = luluCpl_emit_if_jump(cpl, CAN_POP);
    parse_precedence(cpl, ls, PREC_AND);
    luluCpl_patch_jump(cpl, end_jump);
}

static void logic_or(Compiler *cpl, Lexer *ls)
{
    int else_jump = luluCpl_emit_if_jump(cpl, !CAN_POP); // Truthy: goto 'end'.
    int end_jump  = luluCpl_emit_jump(cpl); // Falsy: pop LHS, push RHS.
    luluCpl_patch_jump(cpl, else_jump);
    luluCpl_emit_oparg1(cpl, OP_POP, 1);
    parse_precedence(cpl, ls, PREC_OR);
    luluCpl_patch_jump(cpl, end_jump);
}

// 2}}} ------------------------------------------------------------------------

// PREFIX ----------------------------------------------------------------- {{{2

static void literal(Compiler *cpl, Lexer *ls)
{
    switch (ls->consumed.type) {
    case TK_NIL:   luluCpl_emit_oparg1(cpl, OP_NIL, 1); break;
    case TK_TRUE:  luluCpl_emit_opcode(cpl, OP_TRUE);   break;
    case TK_FALSE: luluCpl_emit_opcode(cpl, OP_FALSE);  break;
    default:
        // Should not happen
        break;
    }
}

// Assumes we just consumed a '('.
static void grouping(Compiler *cpl, Lexer *ls)
{
    // Hacky to create a new scope but lets us error at too many C-facing calls.
    // See: https://www.lua.org/source/5.1/lparser.c.html#enterlevel
    luluCpl_begin_scope(cpl);
    expression(cpl, ls);
    luluLex_expect_token(ls, TK_RPAREN, NULL);
    luluCpl_end_scope(cpl);
}

// Assumes the lexer successfully consumed and encoded a number literal.
static void number(Compiler *cpl, Lexer *ls)
{
    Value v = make_number(ls->consumed.data.number);
    luluCpl_emit_constant(cpl, &v);
}

// Assumes we consumed a string literal.
static void string(Compiler *cpl, Lexer *ls)
{
    Value v = make_string(ls->consumed.data.string);
    luluCpl_emit_constant(cpl, &v);
}

static void resolve_fields(Compiler *cpl, Lexer *ls)
{
    for (;;) {
        if (luluLex_match_token(ls, TK_LBRACKET))
            index(cpl, ls);
        else if (luluLex_match_token(ls, TK_PERIOD))
            field(cpl, ls);
        else
            break;
    }
}

// Always pop the key and value assigning in a table constructor.
static void assign_table(Compiler *cpl, Lexer *ls, int t_idx, int k_idx)
{
    expression(cpl, ls);
    luluCpl_emit_oparg3(cpl, OP_SETTABLE, encode_byte3(t_idx, k_idx, 2));
}

static void construct_table(Compiler *cpl, Lexer *ls, int t_idx, int *a_len)
{
    int k_idx = cpl->stack_usage;
    if (luluLex_match_token(ls, TK_IDENT)) {
        luluCpl_emit_identifier(cpl, ls->consumed.data.string);
        if (luluLex_match_token(ls, TK_ASSIGN)) {
            assign_table(cpl, ls, t_idx, k_idx);
        } else {
            // TODO: Allow infix expressions past the identifier
            resolve_fields(cpl, ls);
            *a_len += 1;
        }
    } else if (luluLex_match_token(ls, TK_LBRACKET)) {
        // Technically we are assigning, just not right now.
        index_(cpl, ls, CAN_ASSIGN);
        luluLex_expect_token(ls, TK_ASSIGN, "to assign table field");
        assign_table(cpl, ls, t_idx, k_idx);
    } else {
        expression(cpl, ls);
        *a_len += 1;
    }
    luluLex_match_token(ls, TK_COMMA);
}

static void table(Compiler *cpl, Lexer *ls)
{
    int t_idx  = cpl->stack_usage;
    int total  = 0; // Array length plus hashmap length.
    int a_len  = 0; // Array portion length.
    int offset = luluCpl_emit_table(cpl);
    while (!luluLex_match_token(ls, TK_RCURLY)) {
        construct_table(cpl, ls, t_idx, &a_len);
        total += 1;
    }

    if (a_len > 0) {
        if (a_len > MAX_BYTE3)
            luluLex_error_consumed(ls, "Too many elements in table constructor");
        luluCpl_emit_oparg2(cpl, OP_SETARRAY, encode_byte2(t_idx, a_len));
    }

    if (total > 0)
        luluCpl_patch_table(cpl, offset, total);
}

/**
 * @brief   Originally analogous to `namedVariable()` in the book, but with our
 *          current semantics ours is radically different.
 *
 * @note    Past the first lexeme, assigning of variables is not allowed in Lua.
 *          So this function can only ever perform get operations.
 */
static void variable(Compiler *cpl, Lexer *ls)
{
    String *id = ls->consumed.data.string;
    luluCpl_emit_variable(cpl, id);
}

static OpCode get_unop(TkType type)
{
    switch (type) {
    case TK_NOT:    return OP_NOT;
    case TK_DASH:   return OP_UNM;
    case TK_POUND:  return OP_LEN;
    default:
        // Should not happen
        return OP_RETURN;
    }
}

// Assumes we just consumed an unary operator.
static void unary(Compiler *cpl, Lexer *ls)
{
    TkType type = ls->consumed.type; // Save in stack-frame memory.
    parse_precedence(cpl, ls, PREC_UNARY);
    luluCpl_emit_opcode(cpl, get_unop(type));
}

// 2}}} ------------------------------------------------------------------------

static void expression(Compiler *cpl, Lexer *ls)
{
    parse_precedence(cpl, ls, PREC_ASSIGN + 1);
}

// 1}}} ------------------------------------------------------------------------

// STATEMENTS ------------------------------------------------------------- {{{1

// ASSIGNMENTS ------------------------------------------------------------ {{{2

static void init_assignment(Assignment *list, Assignment *prev)
{
    list->prev = prev;
    list->type = ASSIGN_GLOBAL;
    list->arg  = -1;
}

static void set_assignment(Assignment *list, AssignType type, int arg)
{
    list->type = type;
    list->arg  = arg;
}

static int count_assignments(Assignment *list)
{
    Assignment *node = list;
    int         count = 0;
    while (node != NULL) {
        node   = node->prev;
        count += 1;
    }
    return count;
}

static void emit_assignment_tail(Compiler *cpl, Assignment *list)
{
    if (list == NULL)
        return;

    switch (list->type) {
    case ASSIGN_GLOBAL:
        luluCpl_emit_oparg3(cpl, OP_SETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        luluCpl_emit_oparg1(cpl, OP_SETLOCAL, list->arg);
        break;
    case ASSIGN_TABLE:
        // For assignments we assume the key is always right after the table.
        luluCpl_emit_oparg3(cpl, OP_SETTABLE, encode_byte3(list->arg, list->arg + 1, 1));
        break;
    }
    emit_assignment_tail(cpl, list->prev);

    // After recursion, clean up stack if we emitted table fields as they cannot
    // be implicitly popped due to SETTABLE being a VAR_DELTA pop.
    if (list->type == ASSIGN_TABLE)
        luluCpl_emit_oparg1(cpl, OP_POP, 2);
}

static void emit_assignment(Compiler *cpl, Lexer *ls, Assignment *list)
{
    int idents = count_assignments(list);
    int exprs  = parse_exprlist(cpl, ls);
    adjust_exprlist(cpl, idents, exprs);
    emit_assignment_tail(cpl, list);
}

// Get the table itself if this is the first token in an lvalue, or if this
// is part of a recursive call we need GETTABLE to push a subtable.
static void discharge_subtable(Compiler *cpl, Assignment *list)
{
    switch (list->type) {
    case ASSIGN_GLOBAL:
        luluCpl_emit_oparg3(cpl, OP_GETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        luluCpl_emit_oparg1(cpl, OP_GETLOCAL,  list->arg);
        break;
    case ASSIGN_TABLE:
        luluCpl_emit_opcode(cpl, OP_GETTABLE);
        break;
    }
}

/**
 * @brief   Emit a variable which is used as a table and the fields thereof.
 *          We do this in order to support multiple assignment semantics.
 *
 * @details In `t.k.v = 13` we want:
 *          GETGLOBAL   't'
 *          CONSTANT    'k'
 *          GETTABLE
 *          CONSTANT    'v'
 *          CONSTANT    13
 *          SETTABLE
 *
 * @note    We don't need to "hold on" to the table/fields themselves, but we do
 *          need to track where in the stack the table occurs so the SETTABLE
 *          instruction knows where to look.
 */
static void discharge_assignment(Compiler *cpl, Lexer *ls, Assignment *list)
{
    // Emit the key immediately. In recursive calls we'll emit another GETTABLE,
    // otherwise we'll know the key is +1 after the table when we emit SETTABLE.
    if (ls->consumed.type == TK_PERIOD)
        field_(cpl, ls, CAN_ASSIGN);
    else
        index_(cpl, ls, CAN_ASSIGN);

    // stack_usage - 0 is top, -1 is the key we emitted and -2 is the table.
    set_assignment(list, ASSIGN_TABLE, cpl->stack_usage - 2);
}

// Assumes we consumed an identifier as the first element of a statement.
static void identifier_statement(Compiler *cpl, Lexer *ls, Assignment *list)
{
// No need for recursion, can be done pseudo-iteratively.
Loop:
    if (list->type != ASSIGN_TABLE) {
        String *id      = ls->consumed.data.string;
        int     arg     = luluCpl_resolve_local(cpl, id);
        bool    islocal = (arg != -1);
        if (!islocal)
            arg = luluCpl_identifier_constant(cpl, id);
        set_assignment(list, islocal ? ASSIGN_LOCAL : ASSIGN_GLOBAL, arg);
    }

    switch (ls->lookahead.type) {
    case TK_PERIOD: // Fall through
    case TK_LBRACKET:
        // Emit the table up to this point then mark `list` as a table.
        luluLex_next_token(ls);
        discharge_subtable(cpl, list);
        discharge_assignment(cpl, ls, list);
        goto Loop;
    case TK_ASSIGN:
        luluLex_next_token(ls);
        emit_assignment(cpl, ls, list);
        break;
    case TK_LPAREN:
        luluLex_error_consumed(ls, "Function calls not yet implemented");
        break;
    case TK_COMMA: {
        // Recursive call so chain elements together.
        Assignment next;
        init_assignment(&next, list);
        luluLex_next_token(ls);
        luluLex_expect_token(ls, TK_IDENT, "after ','");
        identifier_statement(cpl, ls, &next);
        break;
    }
    default:
        luluLex_error_consumed(ls, "Unexpected token in identifier statement");
    }
}

// OTHER ------------------------------------------------------------------ {{{3

// Assumes we just consumed the `print` keyword and are now ready to compile a
// stream of expressions to act as arguments.
static void print_statement(Compiler *cpl, Lexer *ls)
{
    bool open = luluLex_match_token(ls, TK_LPAREN);
    int  argc = parse_exprlist(cpl, ls);
    if (open)
        luluLex_expect_token(ls, TK_RPAREN, NULL);
    luluCpl_emit_oparg1(cpl, OP_PRINT, argc);
}

static void do_block(Compiler *cpl, Lexer *ls)
{
    luluCpl_begin_scope(cpl);
Loop:
    switch (ls->lookahead.type) {
    case TK_END:
    case TK_EOF:
        break;
    default:
        declaration(cpl, ls);
        goto Loop;
    }
    luluCpl_end_scope(cpl);
}

static void if_block(Compiler *cpl, Lexer *ls)
{
    luluCpl_begin_scope(cpl);
Loop:
    switch (ls->lookahead.type) {
    case TK_ELSE:
    case TK_ELSEIF:
    case TK_END:
    case TK_EOF:
        break;
    default:
        declaration(cpl, ls);
        goto Loop;
    }
    luluCpl_end_scope(cpl);
}

// Patch `jump` then set it to a new OP_JUMP.
static void discharge_jump(Compiler *cpl, int *jump, bool can_pop)
{
    int prev = *jump;
    *jump = luluCpl_emit_jump(cpl);
    luluCpl_patch_jump(cpl, prev);
    // pop <condition> when truthy.
    if (can_pop)
        luluCpl_emit_oparg1(cpl, OP_POP, 1);
    cpl->stack_usage += 1; // Undo weirdness from POP w/o a corresponding push.
}

// <expression> 'then'
// <expression> 'do'
static void condition(Compiler *cpl, Lexer *ls, TkType who)
{
    expression(cpl, ls);
    luluLex_expect_token(ls, who, "after <condition>");
}

// The recursion is hacky but I can't think of a better way.
// It's necessary so all <if-block> and <elseif-block> know where 'end' is.
// See: https://github.com/crimeraaa/lulu/blob/main/.archive-2024-03-24/compiler.c#L1483
static void if_statement(Compiler *cpl, Lexer *ls)
{
    condition(cpl, ls, TK_THEN);

    // jump over the entire if-block when <condition> is falsy.
    int if_jump = luluCpl_emit_if_jump(cpl, CAN_POP);
    if_block(cpl, ls);

    // <if-block> end should jump over everything that follows.
    discharge_jump(cpl, &if_jump, CAN_POP);
    if (luluLex_match_token(ls, TK_ELSEIF))
        if_statement(cpl, ls);
    if (luluLex_match_token(ls, TK_ELSE))
        do_block(cpl, ls);

    // For all cases (including recursive), they should jump to the same 'end'.
    luluCpl_patch_jump(cpl, if_jump);
}

static void while_loop(Compiler *cpl, Lexer *ls)
{
    int loop_start = luluCpl_start_loop(cpl);
    condition(cpl, ls, TK_DO);

    // Truthy: skip this jump. Falsy: jump over the entire loop body.
    int loop_init = luluCpl_emit_if_jump(cpl, CAN_POP);
    do_block(cpl, ls);
    luluCpl_emit_loop(cpl, loop_start);

    luluCpl_patch_jump(cpl, loop_init);
    luluCpl_emit_oparg1(cpl, OP_POP, 1);
    cpl->stack_usage += 1; // Weirdness from above POP
}

static void add_internal_local(Compiler *cpl, const char *name)
{
    size_t  len = strlen(name);
    String *id  = luluStr_copy(cpl->vm, view_from_len(name, len));
    luluCpl_add_local(cpl, id);
    luluCpl_identifier_constant(cpl, id);
}

static void emit_local(Compiler *cpl, String *id)
{
    luluCpl_init_local(cpl, id);
    luluCpl_identifier_constant(cpl, id);
}

static void numeric_for(Compiler *cpl, Lexer *ls, String *id)
{
    // Order is important due to VM's assumptions about internal loop state.
    add_internal_local(cpl, "(for index)");
    add_internal_local(cpl, "(for limit)");
    add_internal_local(cpl, "(for step)");

    // <for-index>
    emit_local(cpl, id);
    expression(cpl, ls);
    luluCpl_define_locals(cpl, 4);

    // <for-limit>
    luluLex_expect_token(ls, TK_COMMA, "after 'for' index");
    expression(cpl, ls);

    // <for-step>
    if (luluLex_match_token(ls, TK_COMMA)) {
        expression(cpl, ls);
    } else {
        static const Value n = make_number(1);
        luluCpl_emit_constant(cpl, &n);
    }

    // 'do' <block>
    luluLex_expect_token(ls, TK_DO, NULL);
    luluCpl_emit_opcode(cpl, OP_FORPREP);
    int loop_init  = luluCpl_emit_jump(cpl);
    int loop_start = luluCpl_start_loop(cpl);

    do_block(cpl, ls);
    luluCpl_patch_jump(cpl, loop_init);
    luluCpl_emit_opcode(cpl, OP_FORLOOP);
    luluCpl_emit_loop(cpl,  loop_start);
}

static void for_loop(Compiler *cpl, Lexer *ls)
{
    // Scope for internal loop variables.
    luluCpl_begin_scope(cpl);
    luluLex_expect_token(ls, TK_IDENT, "after 'for'");

    String *id = ls->consumed.data.string;
    luluLex_next_token(ls);
    switch (ls->consumed.type) {
    case TK_ASSIGN: // 'for' <identifier> '=' <expression> ',' ...
        numeric_for(cpl, ls, id);
        break;
    case TK_COMMA:  // 'for' <identifier> [',' <identifier>]+ 'in' <expression>
    case TK_IN:
        luluLex_error_consumed(ls, "generic 'for' loop not yet supported");
        break;
    default:
        luluLex_error_consumed(ls, "'=' or 'in' expected");
        break;
    }

    luluCpl_end_scope(cpl);
}

// 3}}} ------------------------------------------------------------------------

// 2}}} ------------------------------------------------------------------------

static void statement(Compiler *cpl, Lexer *ls)
{
    // `declaration()` already consumed a token for us.
    switch (ls->consumed.type) {
    case TK_IDENT: {
        Assignment list;
        init_assignment(&list, NULL); // Ensure no garbage.
        identifier_statement(cpl, ls, &list);
        break;
    }
    case TK_DO:
        do_block(cpl, ls);
        luluLex_expect_token(ls, TK_END, "after block");
        break;
    case TK_PRINT:
        print_statement(cpl, ls);
        break;
    case TK_IF:
        if_statement(cpl, ls);
        luluLex_expect_token(ls, TK_END, "after 'if' body");
        break;
    case TK_WHILE:
        while_loop(cpl, ls);
        luluLex_expect_token(ls, TK_END, "after 'while' loop body");
        break;
    case TK_FOR:
        for_loop(cpl, ls);
        luluLex_expect_token(ls, TK_END, "after 'for' loop body");
        break;
    default:
        luluLex_error_lookahead(ls, "Expected a statement");
        break;
    }
}

// 1}}} ------------------------------------------------------------------------

// DECLARATIONS ----------------------------------------------------------- {{{1

/**
 * @brief   For Lua this only matters for local variables. Analogous to
 *          `varDeclaration()` in the book.
 *
 * @details vardecl     ::= 'local' identlist ['=' exprlist] ';'
 *          identlist   ::= identifier [',' identifier]*
 *          exprlist    ::= expression [',' expression]*
 *
 * @note    We don't emit `OP_SETLOCAL`, since we only need to push the value to
 *          the stack without popping it. We already keep track of info like the
 *          variable name.
 */
static void declare_locals(Compiler *cpl, Lexer *ls)
{
    int idents = 0;
    int exprs  = 0;

    do {
        luluLex_expect_token(ls, TK_IDENT, NULL);
        emit_local(cpl, ls->consumed.data.string);
        idents += 1;
    } while (luluLex_match_token(ls, TK_COMMA));

    if (luluLex_match_token(ls, TK_ASSIGN))
        exprs = parse_exprlist(cpl, ls);
    adjust_exprlist(cpl, idents, exprs);
    luluCpl_define_locals(cpl, idents);
}

void declaration(Compiler *cpl, Lexer *ls)
{
    luluLex_next_token(ls);
    switch (ls->consumed.type) {
    case TK_LOCAL:
        declare_locals(cpl, ls);
        break;
    default:
        statement(cpl, ls);
        break;
    }
    // Lua allows 1 semicolon to terminate statements, but no more.
    luluLex_match_token(ls, TK_SEMICOL);
}

// 1}}} ------------------------------------------------------------------------

// PARSE RULES ------------------------------------------------------------ {{{1

// Assumes the first token is ALWAYS a prefix expression with 0 or more infix
// expressions following it.
static void parse_precedence(Compiler *cpl, Lexer *ls, Precedence prec)
{
    ParseRule *pr = get_parserule(ls->lookahead.type);

    if (pr->prefixfn == NULL) {
        luluLex_error_consumed(ls, "Expected a prefix expression");
        return;
    }
    luluLex_next_token(ls);
    pr->prefixfn(cpl, ls);

    for (;;) {
        pr = get_parserule(ls->lookahead.type);
        if (!(prec <= pr->precedence))
            break;
        luluLex_next_token(ls);
        pr->infixfn(cpl, ls);
    }

    // This function can never consume the `=` token.
    if (luluLex_match_token(ls, TK_ASSIGN))
        luluLex_error_lookahead(ls, "Invalid assignment target");
}

static ParseRule PARSERULES_LOOKUP[] = {
    // TOKEN           PREFIXFN     INFIXFN     PRECEDENCE
    [TK_AND]        = {NULL,        &logic_and, PREC_AND},
    [TK_BREAK]      = {NULL,        NULL,       PREC_NONE},
    [TK_DO]         = {NULL,        NULL,       PREC_NONE},
    [TK_ELSE]       = {NULL,        NULL,       PREC_NONE},
    [TK_ELSEIF]     = {NULL,        NULL,       PREC_NONE},
    [TK_END]        = {NULL,        NULL,       PREC_NONE},
    [TK_FALSE]      = {&literal,    NULL,       PREC_NONE},
    [TK_FOR]        = {NULL,        NULL,       PREC_NONE},
    [TK_FUNCTION]   = {NULL,        NULL,       PREC_NONE},
    [TK_IF]         = {NULL,        NULL,       PREC_NONE},
    [TK_IN]         = {NULL,        NULL,       PREC_NONE},
    [TK_LOCAL]      = {NULL,        NULL,       PREC_NONE},
    [TK_NIL]        = {&literal,    NULL,       PREC_NONE},
    [TK_NOT]        = {&unary,      NULL,       PREC_NONE},
    [TK_OR]         = {NULL,        &logic_or,  PREC_OR},
    [TK_PRINT]      = {NULL,        NULL,       PREC_NONE},
    [TK_RETURN]     = {NULL,        NULL,       PREC_NONE},
    [TK_THEN]       = {NULL,        NULL,       PREC_NONE},
    [TK_TRUE]       = {&literal,    NULL,       PREC_NONE},
    [TK_WHILE]      = {NULL,        NULL,       PREC_NONE},

    [TK_LPAREN]     = {&grouping,   NULL,       PREC_NONE},
    [TK_RPAREN]     = {NULL,        NULL,       PREC_NONE},
    [TK_LBRACKET]   = {NULL,        &index,     PREC_CALL},
    [TK_RBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_LCURLY]     = {&table,      NULL,       PREC_NONE},
    [TK_RCURLY]     = {NULL,        NULL,       PREC_NONE},

    [TK_COMMA]      = {NULL,        NULL,       PREC_NONE},
    [TK_SEMICOL]    = {NULL,        NULL,       PREC_NONE},
    [TK_VARARG]     = {NULL,        NULL,       PREC_NONE},
    [TK_CONCAT]     = {NULL,        &concat,    PREC_CONCAT},
    [TK_PERIOD]     = {NULL,        &field,     PREC_CALL},
    [TK_POUND]      = {&unary,      NULL,       PREC_UNARY},

    [TK_PLUS]       = {NULL,        &binary,    PREC_TERMINAL},
    [TK_DASH]       = {&unary,      &binary,    PREC_TERMINAL},
    [TK_STAR]       = {NULL,        &binary,    PREC_FACTOR},
    [TK_SLASH]      = {NULL,        &binary,    PREC_FACTOR},
    [TK_PERCENT]    = {NULL,        &binary,    PREC_FACTOR},
    [TK_CARET]      = {NULL,        &binary,    PREC_POW},

    [TK_ASSIGN]     = {NULL,        NULL,       PREC_NONE},
    [TK_EQ]         = {NULL,        &binary,    PREC_EQUALITY},
    [TK_NEQ]        = {NULL,        &binary,    PREC_EQUALITY},
    [TK_GT]         = {NULL,        &binary,    PREC_COMPARISON},
    [TK_GE]         = {NULL,        &binary,    PREC_COMPARISON},
    [TK_LT]         = {NULL,        &binary,    PREC_COMPARISON},
    [TK_LE]         = {NULL,        &binary,    PREC_COMPARISON},

    [TK_IDENT]      = {&variable,   NULL,       PREC_NONE},
    [TK_STRING]     = {&string,     NULL,       PREC_NONE},
    [TK_NUMBER]     = {&number,     NULL,       PREC_NONE},
    [TK_ERROR]      = {NULL,        NULL,       PREC_NONE},
    [TK_EOF]        = {NULL,        NULL,       PREC_NONE},
};

static ParseRule *get_parserule(TkType key)
{
    return &PARSERULES_LOOKUP[key];
}

// 1}}} ------------------------------------------------------------------------
