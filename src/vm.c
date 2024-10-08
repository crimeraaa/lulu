/// local
#include "vm.h"
#include "debug.h"
#include "compiler.h"

/// standard
#include <stdarg.h>     // va_list
#include <stdio.h>      // [vf]printf()
#include <stdlib.h>     // exit()

static void
reset_stack(lulu_VM *vm)
{
    vm->base = &vm->values[0];
    vm->top  = vm->base;
    vm->end  = vm->base + LULU_VM_STACK_MAX;
}

static void
init_table(lulu_Table *table)
{
    lulu_Table_init(table);
    lulu_Object *base = &table->base;
    base->type = LULU_TYPE_TABLE;
    base->next = NULL;
}

void
lulu_VM_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data)
{
    reset_stack(self);
    init_table(&self->strings);
    init_table(&self->globals);
    lulu_Builder_init(self, &self->builder);
    self->allocator      = allocator;
    self->allocator_data = allocator_data;
    self->chunk          = NULL;
    self->objects        = NULL;
    self->handlers       = NULL;
}

void
lulu_VM_free(lulu_VM *self)
{
    lulu_Table_free(self, &self->strings);
    lulu_Table_free(self, &self->globals);
    lulu_Builder_free(&self->builder);

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
    return &vm->top[offset];
}

static void
check_numeric(lulu_VM *vm, cstring act, int lhs_offset, int rhs_offset)
{
    if (lulu_is_number(vm, lhs_offset) && lulu_is_number(vm, rhs_offset)) {
        return;
    }
    lulu_VM_runtime_error(vm, "Attempt to %s %s with %s",
        act, lulu_typename(vm, lhs_offset), lulu_typename(vm, rhs_offset));
}

static void
check_arith(lulu_VM *vm, int lhs_offset, int rhs_offset)
{
    check_numeric(vm, "perform arithmetic on", lhs_offset, rhs_offset);
}

static void
check_compare(lulu_VM *vm, int lhs_offset, int rhs_offset)
{
    check_numeric(vm, "compare", lhs_offset, rhs_offset);
}

static void
concat(lulu_VM *vm, int count)
{
    lulu_Value   *args    = &vm->top[-count];
    lulu_Builder *builder = &vm->builder;

    lulu_Builder_reset(builder);
    for (int i = 0; i < count; i++) {
        lulu_Value *value = &args[i];
        if (!lulu_Value_is_string(value)) {
            lulu_VM_runtime_error(vm, "Attempt to concatenate a %s value",
                lulu_Value_typename(value));
        }
        lulu_Builder_write_string(builder, value->string->data, value->string->len);
    }

    isize        len;
    const char  *data = lulu_Builder_to_string(builder, &len);
    lulu_pop(vm, count);
    lulu_push_string(vm, data, len);
}

static lulu_Status
execute_bytecode(lulu_VM *self)
{
    lulu_Chunk       *chunk     = current_chunk(self);
    lulu_Value_Array *constants = &chunk->constants;

#define BINARY_OP(lulu_Value_set_fn, lulu_Number_fn, check_fn)                 \
do {                                                                           \
    check_fn(self, -2, -1);                                                    \
    lulu_Value *lhs = poke_top(self, -2);                                      \
    lulu_Value *rhs = poke_top(self, -1);                                      \
    lulu_Value_set_fn(lhs, lulu_Number_fn(lhs->number, rhs->number));          \
    lulu_pop(self, 1);                                                         \
} while (0)

#define ARITH_OP(lulu_Number_fn)    BINARY_OP(lulu_Value_set_number,  lulu_Number_fn, check_arith)
#define COMPARE_OP(lulu_Number_fn)  BINARY_OP(lulu_Value_set_boolean, lulu_Number_fn, check_compare)

    for (;;) {
#ifdef LULU_DEBUG_TRACE
        printf("        ");
        for (const lulu_Value *slot = self->base; slot < self->top; slot++) {
            printf("[ ");
            lulu_Debug_print_value(slot);
            printf(" ]");
        }
        printf("\n");
        lulu_Debug_disassemble_instruction(chunk, self->ip - chunk->code);
#endif
        lulu_Instruction inst = *self->ip++;
        switch (lulu_Instruction_get_opcode(inst)) {
        case OP_CONSTANT: {
            byte3 index = lulu_Instruction_get_byte3(inst);
            lulu_VM_push(self, &constants->values[index]);
            break;
        }
        case OP_GETGLOBAL: {
            byte3             index = lulu_Instruction_get_byte3(inst);
            const lulu_Value *key   = &constants->values[index];
            const lulu_Value *value = lulu_Table_get(&self->globals, key);
            if (!value) {
                lulu_VM_runtime_error(self, "Undefined global '%s'", key->string->data);
            }
            lulu_VM_push(self, value);
            break;
        }
        case OP_SETGLOBAL: {
            byte3             index = lulu_Instruction_get_byte3(inst);
            const lulu_Value *ident = &constants->values[index];
            lulu_Table_set(self, &self->globals, ident, poke_top(self, -1));
            lulu_pop(self, 1);
            break;
        }
        case OP_NIL: {
            int count = lulu_Instruction_get_byte1(inst);
            lulu_push_nil(self, count);
            break;
        }
        case OP_TRUE:  lulu_push_boolean(self, true);  break;
        case OP_FALSE: lulu_push_boolean(self, false); break;
        case OP_ADD: ARITH_OP(lulu_Number_add); break;
        case OP_SUB: ARITH_OP(lulu_Number_sub); break;
        case OP_MUL: ARITH_OP(lulu_Number_mul); break;
        case OP_DIV: ARITH_OP(lulu_Number_div); break;
        case OP_MOD: ARITH_OP(lulu_Number_mod); break;
        case OP_POW: ARITH_OP(lulu_Number_pow); break;
        case OP_CONCAT: {
            int count = lulu_Instruction_get_byte1(inst);
            concat(self, count);
            break;
        }
        case OP_UNM: {
            if (lulu_is_number(self, -1)) {
                lulu_Value *value = poke_top(self, -1);
                lulu_Value_set_number(value, lulu_Number_unm(value->number));
            } else {
                lulu_VM_runtime_error(self, "Attempt to negate a %s value",
                    lulu_typename(self, -1));
            }
            break;
        }
        case OP_EQ: {
            lulu_Value *rhs = poke_top(self, -1);
            lulu_Value *lhs = poke_top(self, -2);
            lulu_Value_set_boolean(lhs, lulu_Value_eq(lhs, rhs));
            lulu_pop(self, 1);
            break;
        }
        case OP_LT:  COMPARE_OP(lulu_Number_lt);  break;
        case OP_LEQ: COMPARE_OP(lulu_Number_leq); break;
        case OP_NOT: {
            lulu_Value *value = poke_top(self, -1);
            lulu_Value_set_boolean(value, lulu_Value_is_falsy(value));
            break;
        }
        case OP_PRINT: {
            int count = lulu_Instruction_get_byte1(inst);
            for (int i = 0; i < count; i++) {
                lulu_Value_print(poke_top(self, -count + i));
                printf("\t");
            }
            lulu_pop(self, count);
            printf("\n");
            break;
        }
        case OP_POP: {
            int count = lulu_Instruction_get_byte1(inst);
            lulu_pop(self, count);
            break;
        }
        case OP_RETURN: {
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
    lulu_check_stack(self, 1);
    *self->top = *value;
    self->top++;
}

lulu_Status
lulu_VM_run_protected(lulu_VM *self, lulu_ProtectedFn fn, void *userdata)
{
    lulu_Error_Handler handler;

    // Chain new error handler
    handler.status = LULU_OK;
    handler.prev   = self->handlers;
    self->handlers = &handler;

    LULU_IMPL_TRY(&handler) {
        fn(self, userdata);
    } LULU_IMPL_CATCH(&handler) {
        // empty but braces required in case of C++ 'catch'
    }

    // Restore old error handler
    self->handlers = handler.prev;
    return handler.status;
}

void
lulu_VM_throw_error(lulu_VM *self, lulu_Status status)
{
    lulu_Error_Handler *handler = self->handlers;
    if (handler) {
        handler->status = status;
        LULU_IMPL_THROW(handler);
    } else {
        lulu_Debug_fatal("Unexpected error. Aborting.");
        exit(EXIT_FAILURE);
    }
}

void
lulu_VM_comptime_error(lulu_VM *self, cstring file, int line, cstring msg, const char *where, isize len)
{
    fprintf(stderr, "%s:%i: %s at '%.*s'\n", file, line, msg, cast(int)len, where);
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
    reset_stack(self);
    lulu_VM_throw_error(self, LULU_ERROR_RUNTIME);
}
