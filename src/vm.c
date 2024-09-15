#include "vm.h"
#include "debug.h"
#include "compiler.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static void
stack_init(lulu_Stack *self)
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
    self->objects        = NULL;
    self->handlers       = NULL;
}

void
lulu_VM_free(lulu_VM *self)
{
    lulu_Object *object = self->objects;
    while (object) {
        lulu_Object *next = object->next;
        lulu_Object_free(self, object);
        object = next;
    }
}

static lulu_Chunk *
current_chunk(lulu_VM *self)
{
    return self->chunk;
}

static lulu_Value *
poke_top(lulu_VM *vm, int offset)
{
    return &vm->stack.top[offset];
}

static void
check_numeric(lulu_VM *vm, cstring act, const lulu_Value *lhs, const lulu_Value *rhs)
{
    if (lulu_Value_is_number(lhs) && lulu_Value_is_number(rhs)) {
        return;
    }
    lulu_VM_runtime_error(vm, "Attempt to %s %s with %s",
        act, lulu_Value_typename(lhs), lulu_Value_typename(rhs));
}

static void
check_arith(lulu_VM *vm, const lulu_Value *lhs, const lulu_Value *rhs)
{
    check_numeric(vm, "perform arithmetic on", lhs, rhs);
}

static void
check_compare(lulu_VM *vm, const lulu_Value *lhs, const lulu_Value *rhs)
{
    check_numeric(vm, "compare", lhs, rhs);
}

static lulu_Status
execute_bytecode(lulu_VM *self)
{
    lulu_Chunk *chunk = current_chunk(self);

#define BINARY_OP(lulu_Value_set_fn, lulu_Number_fn, check_fn)                 \
do {                                                                           \
    lulu_Value *rhs = poke_top(self, -1);                                      \
    lulu_Value *lhs = poke_top(self, -2);                                      \
    check_fn(self, lhs, rhs);                                                  \
    lulu_Value_set_fn(lhs, lulu_Number_fn(lhs->number, rhs->number));          \
    lulu_VM_pop(self);                                                         \
} while (0)
    
#define ARITH_OP(lulu_Number_fn)    BINARY_OP(lulu_Value_set_number,  lulu_Number_fn, check_arith)
#define COMPARE_OP(lulu_Number_fn)  BINARY_OP(lulu_Value_set_boolean, lulu_Number_fn, check_compare)
    
    for (;;) {
        lulu_Instruction inst;
#ifdef LULU_DEBUG_TRACE
        const lulu_Stack *stack = &self->stack;
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
            int count = lulu_Instruction_get_byte1(inst);
            for (int i = 0; i < count; i++) {
                lulu_VM_push(self, &LULU_VALUE_NIL);
            }
            break;
        }
        case OP_TRUE:  lulu_VM_push(self, &LULU_VALUE_TRUE);  break;
        case OP_FALSE: lulu_VM_push(self, &LULU_VALUE_FALSE); break;
        case OP_ADD: ARITH_OP(lulu_Number_add); break;
        case OP_SUB: ARITH_OP(lulu_Number_sub); break;
        case OP_MUL: ARITH_OP(lulu_Number_mul); break;
        case OP_DIV: ARITH_OP(lulu_Number_div); break;
        case OP_MOD: ARITH_OP(lulu_Number_mod); break;
        case OP_POW: ARITH_OP(lulu_Number_pow); break;
        case OP_UNM: {
            lulu_Value *value = poke_top(self, -1);
            if (!lulu_Value_is_number(value)) {
                lulu_VM_runtime_error(self,
                    "Attempt to negate a %s value", lulu_Value_typename(value));
            }
            lulu_Value_set_number(value, lulu_Number_unm(value->number));
            break;
        }
        case OP_EQ: {
            lulu_Value *rhs = poke_top(self, -1);
            lulu_Value *lhs = poke_top(self, -2);
            lulu_Value_set_boolean(lhs, lulu_Value_eq(lhs, rhs));
            lulu_VM_pop(self);
            break;
        }
        case OP_LT:  COMPARE_OP(lulu_Number_lt);  break;
        case OP_LEQ: COMPARE_OP(lulu_Number_leq); break;
        
        case OP_NOT: {
            lulu_Value *value = poke_top(self, -1);
            lulu_Value_set_boolean(value, lulu_Value_is_falsy(value));
            break;
        }
        case OP_RETURN: {
            lulu_Value value = lulu_VM_pop(self);
            lulu_Debug_print_value(&value);
            printf("\n");
            return LULU_OK;
        }
        default:
            __builtin_unreachable();
        }
    }
    
#undef COMPARE_OP
#undef ARITH_OP
#undef BINARY_OP

}

typedef struct {
    lulu_Compiler compiler;
    lulu_Chunk    chunk;
    cstring       input;
} Comptime;

static void
compile_and_run(lulu_VM *self, void *userdata)
{
    Comptime *ud = cast(Comptime *)userdata;
    lulu_Compiler_init(self, &ud->compiler);
    lulu_Compiler_compile(&ud->compiler, ud->input, &ud->chunk);

    self->chunk = &ud->chunk;
    self->ip    = self->chunk->code;
    execute_bytecode(self);
}

lulu_Status
lulu_VM_interpret(lulu_VM *self, cstring name, cstring input)
{
    Comptime ud;
    ud.input = input;
    lulu_Chunk_init(&ud.chunk, name);
    lulu_Status status = lulu_VM_run_protected(self, &compile_and_run, &ud);
    lulu_Chunk_free(self, &ud.chunk);
    return status;
}

void
lulu_VM_push(lulu_VM *self, const lulu_Value *value)
{
    lulu_Stack *stack = &self->stack;
    lulu_Debug_assert(stack->top < stack->end, "VM stack overflow");
    *stack->top = *value;
    stack->top++;
}

lulu_Value
lulu_VM_pop(lulu_VM *self)
{
    lulu_Stack *stack = &self->stack;
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
    va_list     args;
    lulu_Chunk *chunk = current_chunk(self);
    cstring     file  = chunk->filename;
    int         line  = chunk->lines[self->ip - chunk->code - 1];
    va_start(args, fmt);
    fprintf(stderr, "%s:%i: ", file, line);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
    
    // Ensure stack is valid for the next run.
    stack_init(&self->stack);
    lulu_VM_throw_error(self, LULU_ERROR_RUNTIME);
}
