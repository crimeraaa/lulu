# Parsing Notes

Our text:

```lua
print("Hi mom!")
```

1. `print`

```cpp

// A. From `chunk()`
lparser.c:statement(LexState *lex) {
    .Status: {
        lex->current.type = Token_Name,
        lex->lookahead.type = Token_Eos,
        lex->func.freereg = 0,
    };
    default: expr_stmt(lex); // default case
}

// B.
lparser.c:expr_stmt(LexState *lex) {
    struct LHS_assign v;
    primaryexp(lex, var=&v.var);
}

// C.
lparser.c:primaryexp(LexState *lex, Expr *var) {
    FuncState *func = lex->func;
    prefixexp(lex, var);    
    .Status: {
        lex->current.type = Token_Left_Paren;
        var = {.kind = Expr_Global, .info = 0};
    };

    // switch (lex->current.type) // 1st iteration
    case Token_Left_Paren: {
        luaK_exp2nextreg(func, var);
        .Status: {
            func->freereg = 1,
            expr = {.kind = Expr_Nonrelocable, .info = 0},
        };

        funcargs(lex, var);
        break;
    }
}

// D-1.
lparser.c:prefixexp(LexState *lex, Expr *var) {
    switch (lex->current.type) {
        case Token_Name: 
            singlevar(lex, var);
            .Status:
                lex->current.type = Token_Left_Paren;
                var = {.kind = Expr_Global, .info = 0};
            return;
    }
}

// E.
lparser.c:singlevar(LexState *lex, Expr *var) {
    TString *varname = str_checkname(lex) = "print";
    .Status: lex->current.type = Token_Left_Paren

    singlevaraux(lex->func, varname, var, true) == Expr_Global
        var->u.s.info = luaK_stringK(func, varname);

    .Status: var = {.kind = Expr_Global, .info = 0}
}

// D-2.
luaK_exp2nextreg(FuncState *func, Expr *expr) {
    luaK_dischargevars(func, expr);
    .Status: expr = {.kind = Expr_Relocable, .info = 0}

    freeexp(func, expr);
    luaK_reserveregs(func, 1);
    .Status: func->freereg = 1

    exp2reg(func, expr, func->freereg - 1);
    .Status: expr = {.kind = Expr_Nonrelocable, .info = 0};
}

// E-1.
luaK_dischargevars(FuncState *func, Expr *expr) {
    // switch (expr->kind)
    case Expr_Global:
        // func->pc == 0
        expr->u.s.info = luaK_codeABx(func, OP_GETGLOBAL, 0, expr->u.s.info);
        expr->kind = Expr_Relocable;
        break;

    .Status: expr = {.kind = Expr_Relocable, .info = 0}
}

// E-2.
lcode.c:freeexp(FuncState *func, Expr *expr) {
    // expr->kind != Expr_Nonrelocable so do nothing
}

// E-3.
luaK_reserveregs(FuncState *func, int n = 1) {
    luaK_checkstack(func, n); // assume this always passes
    func->freereg += n // (freereg = 0) + (n = 1)
    
    .Status: func->freereg = 1
}

// E-4.
lcode.c:exp2reg(FuncState *func, Expr *expr, int reg = func->freereg - 1 = 0) {
    discharge2reg(func, expr, reg);
    .Status: expr = {.kind = Expr_Nonrelocable, .info = 0};

    // expr->kind != Expr_Jump so skip `if`
    // !hasjumps(expr) so skip next `if`

    expr->f = expr->t = NO_JUMP;
    expr->u.s.info = reg;
    expr->kind = Expr_Nonrelocable;
    .Status: expr = {.kind = Expr_Nonrelocable, .info = 0};
}

// F.
lcode.c:discharge2reg(FuncState *func, Expr *expr, int reg = 0) {
    luaK_dischargevars(func, expr); // default case; nothing to do

    // switch (expr->kind)
    case Expr_Relocable: {
        Instruction *pc = &func->proto->code[expr->u.s.info]; // &code[0];
        SETARG_A(*pc, reg); // arg A = 0
        break;
    }
    
    expr->u.s.info = reg; // 0
    expr->kind = Expr_Nonrelocable;

    .Status: expr = {.kind = Expr_Nonrelocable, .info = 0};
}

// D-3.)
lparser.c:funcargs(LexState *lex, Expr *expr) {
    Expr args;
    int base, nparams;
    .Status:
        lex->current.type = Token_Left_Paren

    // switch (lex->current.type)
    case Token_Left_Paren: {
        luaX_next(lex);
        .Status:
            lex->current = {.type = Token_String, .seminfo = "Hi mom!"}

        explist1(lex, var=&args);
        luaK_setreturns(func, &args, LUA_MULTRET); // luaK_setmultret
        check_match(lex, Token_Right_Paren, Token_Left_Paren, line);
    }
    
    lua_assert(expr->kind == Expr_Nonrelocable);
    base = expr->u.s.info;

    // !hasmultret(args.kind)
        //  args.kind != Expr_Void
            luaK_exp2nextreg(func, &args);
        nparams = func->freereg - (base + 1);

    init_exp(expr, Expr_Call, luaK_codeABC(func, OP_CALL, base, nparams + 1, 2));
    luaK_fixline(func, line);
    func->freereg = base + 1;
}

// E-1.
lparser.c:explist1(LexState *lex, Expr *var) {
    int n = 1;
    expression(lex, var);
    
    // test_next(lex, Token_Comma) == false so no loop
    return n;
}

// F.
lparser.c:expression(LexState *lex, Expr *var) {
    subexpr(lex, var, 0);
}

// G-1.
lparser.c:subexpr(LexState *lex, Expr *var, unsigned int limit) {
    BinOpr op;
    UnOpr uop;
    .Status:
        lex->current.type = Token_String
        
    uop = getunopr(lex->current.type);
    (uop == OPR_NOUNOPR) -> {
        simpleexp(lex, var);
        .Status: {
            lex->current.type = Token_Right_Paren,
            var = {.kind = Expr_Constant, info = 1},
        }
    } 
}

// H-1
lparser.c:simpleexp(LexState *lex, Expr *var) {
    switch (lex->current.type) {
        case Token_String: {
            codestring(lex, expr=var, lex->current.seminfo.ts);
            .Status:
                var = {.kind = Expr_Constant, info = 1};
            break;
        }
    }
    
    luaX_next(lex);
    .Status:
        lex->current.type = Token_Right_Paren;
}


```

2. `(`
3. `"Hi mom!"`
4. `)`
