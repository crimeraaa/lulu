#include "vm.h"
#include "debug.h"
#include "compiler.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>

static void stack_init(lulu_VM_Stack *self)
{
    self->base = &self->values[0];
    self->top  = self->base;
    self->end  = self->base + LULU_VM_STACK_MAX;
}


void lulu_VM_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data)
{
    stack_init(&self->stack);
    self->allocator      = allocator;
    self->allocator_data = allocator_data;
    self->chunk          = NULL;
    self->handlers       = NULL;
}

void lulu_VM_free(lulu_VM *self)
{
    unused(self);
}

static lulu_Status execute_bytecode(lulu_VM *self)
{

#define READ_BYTE()     (*self->ip++)
#define READ_BYTE3()    READ_BYTE() | (READ_BYTE() << 8) | (READ_BYTE() << 16)
#define READ_CONSTANT() (self->chunk->constants.values[READ_BYTE3()])

#define BINARY_OP(lulu_Number_fn)                                              \
do {                                                                           \
    lulu_Value rhs = lulu_VM_pop(self);                                        \
    lulu_Value lhs = lulu_VM_pop(self);                                        \
    lulu_Value result;                                                         \
    lulu_Value_set_number(&result, lulu_Number_fn(lhs.number, rhs.number));    \
    lulu_VM_push(self, &result);                                               \
} while (0)
    
    for (;;) {
#ifdef LULU_DEBUG_TRACE
        const lulu_VM_Stack *stack = &self->stack;
        printf("        ");
        for (const lulu_Value *slot = stack->base; slot < stack->top; slot++) {
            printf("[ ");
            lulu_Debug_print_value(slot);
            printf(" ]");
        }
        printf("\n");
        lulu_Debug_disassemble_instruction(self->chunk, self->ip - self->chunk->code);
#endif
        byte inst = READ_BYTE();
        switch (inst) {
        case OP_CONSTANT: {
            lulu_Value value = READ_CONSTANT();
            lulu_VM_push(self, &value);
            break;
        }
        case OP_ADD: BINARY_OP(lulu_Number_add); break;
        case OP_SUB: BINARY_OP(lulu_Number_sub); break;
        case OP_MUL: BINARY_OP(lulu_Number_mul); break;
        case OP_DIV: BINARY_OP(lulu_Number_div); break;
        case OP_MOD: BINARY_OP(lulu_Number_mod); break;
        case OP_POW: BINARY_OP(lulu_Number_pow); break;
        case OP_UNM: {
            lulu_Value value = lulu_VM_pop(self);
            lulu_Value_set_number(&value, lulu_Number_unm(value.number));
            lulu_VM_push(self, &value);
            break;
        }
        case OP_RETURN: {
            lulu_Value value = lulu_VM_pop(self);
            lulu_Debug_print_value(&value);
            printf("\n");
            return LULU_OK;
        }
        }
    }

    // Unreachable but maybe we have corrupt data!
    return LULU_ERROR_RUNTIME;
    
#undef BINARY_OP
#undef READ_BYTE
#undef READ_BYTE3
#undef READ_CONSTANT

}

typedef struct {
    lulu_Compiler compiler;
    lulu_Lexer    lexer;
    lulu_Chunk    chunk;
    cstring       input;
} Comptime;

static void compile_only(lulu_VM *self, void *userdata)
{
    Comptime *ud = cast(Comptime *)userdata;
    unused(self);
    lulu_Compiler_compile(&ud->compiler, ud->input, &ud->chunk);
}

static void compile_and_run(lulu_VM *self, void *userdata)
{
    lulu_Status status;
    Comptime   *ud = cast(Comptime *)userdata;
    lulu_Chunk_init(&ud->chunk);

    status = lulu_VM_run_protected(self, &compile_only, ud);

    // Only execute if we're certain the chunk is valid.
    if (status == LULU_OK) {
        self->chunk = &ud->chunk;
        self->ip    = self->chunk->code;
        
        /**
         * @todo 2024-09-06
         *      We can just raise errors from `execute_bytecode()` directly.
         *      If there are no errors, `lulu_VM_run_protected()` defaults to
         *      `LULU_OK`.
         */
        status = execute_bytecode(self);
    }

    // This must always be run even when an error is thrown.
    lulu_Chunk_free(self, &ud->chunk);

    // Indirectly set the return value of  `lulu_VM_run_protected()`.
    if (status != LULU_OK) {
        lulu_VM_throw_error(self, status);
    }
}

lulu_Status lulu_VM_interpret(lulu_VM *self, cstring input)
{
    Comptime ud;
    lulu_Compiler_init(self, &ud.compiler, &ud.lexer);
    ud.input = input;
    return lulu_VM_run_protected(self, &compile_and_run, &ud);
}

void lulu_VM_push(lulu_VM *self, const lulu_Value *value)
{
    lulu_VM_Stack *stack = &self->stack;
    lulu_Debug_assert(stack->top < stack->end, "VM stack overflow");
    *stack->top = *value;
    stack->top++;
}

lulu_Value lulu_VM_pop(lulu_VM *self)
{
    lulu_VM_Stack *stack = &self->stack;
    lulu_Debug_assert(stack->top > stack->base, "VM stack underflow");
    stack->top--;
    return *stack->top;
}

lulu_Status lulu_VM_run_protected(lulu_VM *self, lulu_ProtectedFn fn, void *userdata)
{
    lulu_Handler handler;
    handler.status = LULU_OK;
    handler.prev   = self->handlers; // Chain new error handler
    self->handlers = &handler;
    
    // setjmp only returns 0 on the very first try, any call to longjmp will
    // bring you back here with a nonzero return value.
    if (setjmp(handler.jump) == 0) {
        fn(self, userdata);
    }

    self->handlers = handler.prev; // Restore old error handler
    return handler.status;
}

void lulu_VM_throw_error(lulu_VM *self, lulu_Status status)
{
    lulu_Handler *handler = self->handlers;
    if (handler) {
        handler->status = status;
        longjmp(handler->jump, 1);
    } else {
        fprintf(stderr, "[FATAL]: %s\n", "Unprotected error. Aborting.");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }
}

void lulu_VM_comptime_error(lulu_VM *self, int line, cstring msg, String where)
{
    fprintf(stderr, "line %i: %s at '%.*s'\n", line, msg, cast(int)where.len, where.data);
    lulu_VM_throw_error(self, LULU_ERROR_COMPTIME);
}
