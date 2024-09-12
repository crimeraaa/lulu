#include "vm.h"
#include "debug.h"
#include "compiler.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static void
stack_init(lulu_VM_Stack *self)
{
    self->base = &self->values[0];
    self->top  = self->base;
    self->end  = self->base + LULU_VM_STACK_MAX;
}


void
lulu_VM_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data)
{
    stack_init(&self->stack);
    self->allocator      = allocator;
    self->allocator_data = allocator_data;
    self->chunk          = NULL;
    self->handlers       = NULL;
}

void
lulu_VM_free(lulu_VM *self)
{
    unused(self);
}

static lulu_Chunk *
current_chunk(lulu_VM *self)
{
    return self->chunk;
}

static void
check_arith(lulu_VM *vm, const lulu_Value *lhs, const lulu_Value *rhs)
{
    if (lulu_Value_is_number(lhs) && lulu_Value_is_number(rhs)) {
        return;
    }
    lulu_VM_runtime_error(vm, "Attempt to perform arithmetic on %s with %s",
        lulu_Value_typename(lhs), lulu_Value_typename(rhs));
}

static lulu_Status
execute_bytecode(lulu_VM *self)
{
    lulu_Chunk *chunk = current_chunk(self);

#define BINARY_OP(lulu_Value_set_fn, lulu_Number_fn, check_fn)                 \
do {                                                                           \
    lulu_Value *rhs = &self->stack.top[-1];                                    \
    lulu_Value *lhs = &self->stack.top[-2];                                    \
    check_fn(self, lhs, rhs);                                                  \
    lulu_Value_set_fn(lhs, lulu_Number_fn(lhs->number, rhs->number));          \
    lulu_VM_pop(self);                                                         \
} while (0)
    
#define ARITH_OP(lulu_Number_fn)    BINARY_OP(lulu_Value_set_number,  lulu_Number_fn, check_arith)
#define COMPARE_OP(lulu_Number_fn)  BINARY_OP(lulu_Value_set_boolean, lulu_Number_fn, check_arith)
    
    for (;;) {
        lulu_Instruction inst;
#ifdef LULU_DEBUG_TRACE
        const lulu_VM_Stack *stack = &self->stack;
        printf("        ");
        for (const lulu_Value *slot = stack->base; slot < stack->top; slot++) {
            printf("[ ");
            lulu_Debug_print_value(slot);
            printf(" ]");
        }
        printf("\n");
        lulu_Debug_disassemble_instruction(chunk, self->ip - chunk->code);
#endif
        inst = *self->ip++;
        switch (lulu_Instruction_get_opcode(inst)) {
        case OP_CONSTANT: {
            byte3      index = lulu_Instruction_get_byte3(inst);
            lulu_Value value = chunk->constants.values[index];
            lulu_VM_push(self, &value);
            break;
        }
        case OP_NIL: {
            lulu_Value value;
            lulu_Value_set_nil(&value);
            lulu_VM_push(self, &value);
            break;
        }
        case OP_TRUE: {
            lulu_Value value;
            lulu_Value_set_boolean(&value, true);
            lulu_VM_push(self, &value);
            break;
        }
        case OP_FALSE: {
            lulu_Value value;
            lulu_Value_set_boolean(&value, false);
            lulu_VM_push(self, &value);
            break;
        }
        case OP_ADD: ARITH_OP(lulu_Number_add); break;
        case OP_SUB: ARITH_OP(lulu_Number_sub); break;
        case OP_MUL: ARITH_OP(lulu_Number_mul); break;
        case OP_DIV: ARITH_OP(lulu_Number_div); break;
        case OP_MOD: ARITH_OP(lulu_Number_mod); break;
        case OP_POW: ARITH_OP(lulu_Number_pow); break;
        case OP_UNM: {
            lulu_Value *value = &self->stack.top[-1];
            lulu_Value_set_number(value, lulu_Number_unm(value->number));
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
    
#undef COMPARE_OP
#undef ARITH_OP
#undef BINARY_OP

}

typedef struct {
    lulu_Compiler compiler;
    lulu_Chunk    chunk;
    cstring       name;
    cstring       input;
} Comptime;

static void
compile_only(lulu_VM *self, void *userdata)
{
    Comptime *ud = cast(Comptime *)userdata;
    lulu_Chunk_init(&ud->chunk, ud->name);
    lulu_Compiler_init(self, &ud->compiler);
    lulu_Compiler_compile(&ud->compiler, ud->input, &ud->chunk);
}

static void
compile_and_run(lulu_VM *self, void *userdata)
{
    Comptime   *ud     = cast(Comptime *)userdata;
    lulu_Status status = lulu_VM_run_protected(self, &compile_only, ud);

    // Only execute if we're certain the chunk is valid.
    if (status == LULU_OK) {
        self->chunk = &ud->chunk;
        self->ip    = self->chunk->code;
        
        /**
         * @todo 2024-09-06
         *      We can just raise errors from execute_bytecode() directly.
         *      If there are no errors, lulu_VM_run_protected() defaults to
         *      LULU_OK.
         */
        status = execute_bytecode(self);
    }

    // This must always be run even when an error is thrown.
    lulu_Chunk_free(self, &ud->chunk);

    // Indirectly set the return value of lulu_VM_run_protected().
    if (status != LULU_OK) {
        lulu_VM_throw_error(self, status);
    }
}

lulu_Status
lulu_VM_interpret(lulu_VM *self, cstring name, cstring input)
{
    Comptime ud;
    ud.name  = name;
    ud.input = input;
    return lulu_VM_run_protected(self, &compile_and_run, &ud);
}

void
lulu_VM_push(lulu_VM *self, const lulu_Value *value)
{
    lulu_VM_Stack *stack = &self->stack;
    lulu_Debug_assert(stack->top < stack->end, "VM stack overflow");
    *stack->top = *value;
    stack->top++;
}

lulu_Value
lulu_VM_pop(lulu_VM *self)
{
    lulu_VM_Stack *stack = &self->stack;
    lulu_Debug_assert(stack->top > stack->base, "VM stack underflow");
    stack->top--;
    return *stack->top;
}

lulu_Status
lulu_VM_run_protected(lulu_VM *self, lulu_ProtectedFn fn, void *userdata)
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

void
lulu_VM_throw_error(lulu_VM *self, lulu_Status status)
{
    lulu_Handler *handler = self->handlers;
    if (handler) {
        handler->status = status;
        longjmp(handler->jump, 1);
    } else {
        lulu_Debug_fatal("Unexpected error. Aborting.");
        exit(EXIT_FAILURE);
    }
}

void
lulu_VM_comptime_error(lulu_VM *self, cstring file, int line, cstring msg, String where)
{
    fprintf(stderr, "%s:%i: %s at '%.*s'\n", file, line, msg, cast(int)where.len, where.data);
    lulu_VM_throw_error(self, LULU_ERROR_COMPTIME);
}

void
lulu_VM_runtime_error(lulu_VM *self, cstring fmt, ...)
{
    va_list args;
    cstring file = self->chunk->filename;
    int     line = self->chunk->lines[self->ip - self->chunk->code - 1];
    va_start(args, fmt);
    fprintf(stderr, "%s:%i: ", file, line);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
    
    // Ensure stack is valid for the next run.
    stack_init(&self->stack);
    lulu_VM_throw_error(self, LULU_ERROR_RUNTIME);
}
