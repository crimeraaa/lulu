/// local
#include "vm.h"
#include "debug.h"
#include "compiler.h"
#include "parser.h"

/// standard
#include <stdarg.h> // va_list
#include <stdio.h>  // [vf]printf, fputc
#include <stdlib.h> // exit

static void
reset_stack(lulu_VM *vm)
{
    vm->base = &vm->values[0];
    vm->top  = vm->base;
    vm->end  = vm->base + LULU_VM_STACK_MAX;
}

static void
init_table(Table *table)
{
    table_init(table);
    Object *base = &table->base;
    base->type = LULU_TYPE_TABLE;
    base->next = NULL;
}

static void
init_alloc(lulu_VM *self, void *userdata)
{
    Value value;
    unused(userdata);
    value_set_table(&value, &self->globals);
    lulu_push_literal(self, "_G");
    // Will most likely realloc the globals table!
    table_set(self, &self->globals, &self->top[-1], &value);
    lulu_pop(self, 1); // We only pushed "_G" so pop it.
}

bool
vm_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data)
{
    reset_stack(self);
    init_table(&self->strings);
    init_table(&self->globals);
    builder_init(self, &self->builder);
    self->allocator      = allocator;
    self->allocator_data = allocator_data;
    self->chunk          = NULL;
    self->objects        = NULL;
    self->handlers       = NULL;

    return vm_run_protected(self, &init_alloc, NULL) == LULU_OK;
}

void
vm_free(lulu_VM *self)
{
    table_free(self, &self->strings);
    table_free(self, &self->globals);
    builder_free(&self->builder);

    Object *object = self->objects;
    while (object) {
        Object *next = object->next;
        object_free(self, object);
        object = next;
    }
}

static Chunk *
current_chunk(lulu_VM *self)
{
    return self->chunk;
}

static void
check_numeric(lulu_VM *vm, cstring act, int lhs_offset, int rhs_offset)
{
    if (lulu_is_number(vm, lhs_offset) && lulu_is_number(vm, rhs_offset)) {
        return;
    }
    vm_runtime_error(vm, "Attempt to %s %s with %s",
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

static Number
ensure_number(lulu_VM *vm, const Value *value, cstring action)
{
    if (!value_is_number(value)) {
        vm_runtime_error(vm, "Attempt to %s a %s value", action, value_typename(value));
    }
    return value->number;
}

static OString *
ensure_string(lulu_VM *vm, const Value *value, cstring action)
{
    if (!value_is_string(value)) {
        vm_runtime_error(vm, "Attempt to %s a %s value", action, value_typename(value));
    }
    return value->string;
}

static Table *
ensure_table(lulu_VM *vm, const Value *value, cstring action)
{
    if (!value_is_table(value)) {
        vm_runtime_error(vm, "Attempt to %s a %s value", action, value_typename(value));
    }
    return value->table;
}

static void
concat(lulu_VM *vm, int count)
{
    Value   *args    = &vm->top[-count];
    Builder *builder = &vm->builder;

    builder_reset(builder);
    for (int i = 0; i < count; i++) {
        OString *string = ensure_string(vm, &args[i], "concatenate");
        builder_write_string(builder, string->data, string->len);
    }

    isize        len;
    const char  *data = builder_to_string(builder, &len);
    lulu_pop(vm, count);
    lulu_push_string(vm, data, len);
}

static lulu_Status
vm_execute(lulu_VM *self)
{
    const Chunk *chunk     = current_chunk(self);
    const Value *constants = chunk->constants.values; // Sync with 'chunk'!
    Table *const globals   = &self->globals;

#define BINARY_OP(value_set_fn, lulu_Number_fn, check_fn)                      \
do {                                                                           \
    check_fn(self, -2, -1);                                                    \
    Value *lhs = &top[-2];                                                     \
    Value *rhs = &top[-1];                                                     \
    value_set_fn(lhs, lulu_Number_fn(lhs->number, rhs->number));               \
    lulu_pop(self, 1);                                                         \
} while (0)

#define ARITH_OP(lulu_Number_fn)    BINARY_OP(value_set_number,  lulu_Number_fn, check_arith)
#define COMPARE_OP(lulu_Number_fn)  BINARY_OP(value_set_boolean, lulu_Number_fn, check_compare)

    for (;;) {
        // Reload these each iteration in case they were updated by the API.
        Value *base = self->base;
        Value *top  = self->top;
#ifdef LULU_DEBUG_TRACE
        printf("        ");
        for (const Value *slot = base; slot < top; slot++) {
            printf("[ ");
            debug_print_value(slot);
            printf(" ]");
        }
        printf("\n");
        debug_disassemble_instruction(chunk, self->ip - chunk->code);
#endif
        Instruction inst = *self->ip++;
        switch (instr_get_op(inst)) {
        case OP_CONSTANT:
        {
            byte3 index = instr_get_ABC(inst);
            vm_push(self, &constants[index]);
            break;
        }
        case OP_GET_GLOBAL:
        {
            const byte3  index = instr_get_ABC(inst);
            const Value *key   = &constants[index];
            const Value *value = table_get(globals, key);
            if (!value) {
                vm_runtime_error(self, "Undefined global '%s'", key->string->data);
            }
            vm_push(self, value);
            break;
        }
        case OP_SET_GLOBAL:
        {
            const byte3  index = instr_get_ABC(inst);
            const Value *ident = &constants[index];
            table_set(self, globals, ident, &top[-1]);
            lulu_pop(self, 1);
            break;
        }
        case OP_GET_LOCAL:
        {
            byte index = instr_get_A(inst);
            vm_push(self, &base[index]);
            break;
        }
        case OP_SET_LOCAL:
        {
            byte index = instr_get_A(inst);
            base[index] = top[-1];
            lulu_pop(self, 1);
            break;
        }
        case OP_NEW_TABLE:
        {
            int n_hash  = instr_get_A(inst);
            int n_array = instr_get_B(inst);
            lulu_push_table(self, n_hash, n_array);
            break;
        }
        case OP_GET_TABLE:
        {
            Table       *table = ensure_table(self, &top[-2], "get field");
            const Value *key   = &top[-1];
            const Value *value = table_get(table, key);
            if (!value) {
                value = &LULU_VALUE_NIL;
            }
            lulu_pop(self, 2);  // pop table and key
            vm_push(self, value);   // then push value to top
            break;
        }
        case OP_SET_TABLE:
        {
            int n_pop   = instr_get_A(inst);
            int i_table = instr_get_B(inst);
            int i_key   = instr_get_C(inst);

            // 'i_table' is always positive.
            Table       *table = ensure_table(self, &base[i_table], "get field");
            const Value *key   = &base[i_key];
            const Value *value = &top[-1];
            table_set(self, table, key, value);

            if (n_pop > 0) {
                lulu_pop(self, n_pop);
            }
            break;
        }
        case OP_SET_ARRAY:
        {
            int n_array = instr_get_A(inst);
            int i_table = instr_get_B(inst);

            // 'i_table' is always positive.
            Table  *table = base[i_table].table;
            VArray *array = &table->array;
            for (int i = 0; i < n_array; i++) {
                table_set_array(self, table, array, i + 1, &top[-n_array + i]);
            }
            lulu_pop(self, n_array);
            break;
        }
        case OP_LEN:
        {
            isize  n_len = 0;
            Value *value = &top[-1];
            switch (value->type) {
            case LULU_TYPE_TABLE:  n_len = value->table->array.len; break;
            case LULU_TYPE_STRING: n_len = value->string->len;      break;
            default:
                vm_runtime_error(self, "Attempt to get length of a %s value",
                    value_typename(value));
            }
            value_set_number(value, n_len); // @warning implicit cast: integer-to-float
            break;
        }
        case OP_NIL:
        {
            int n_nils = instr_get_A(inst);
            lulu_push_nil(self, n_nils);
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
            int count = instr_get_A(inst);
            concat(self, count);
            break;
        }
        case OP_UNM: {
            Value *value  = &top[-1];
            Number number = ensure_number(self, value, "negate");
            value_set_number(value, lulu_Number_unm(number));
            break;
        }
        case OP_EQ: {
            Value *rhs = &top[-1];
            Value *lhs = &top[-2];
            value_set_boolean(lhs, value_eq(lhs, rhs));
            lulu_pop(self, 1);
            break;
        }
        case OP_LT:  COMPARE_OP(lulu_Number_lt);  break;
        case OP_LEQ: COMPARE_OP(lulu_Number_leq); break;
        case OP_NOT: {
            Value *value = &top[-1];
            value_set_boolean(value, value_is_falsy(value));
            break;
        }
        case OP_PRINT: {
            int n_args = instr_get_A(inst);
            for (int i = 0; i < n_args; i++) {
                value_print(&top[-n_args + i]);
                printf("\t");
            }
            lulu_pop(self, n_args);
            printf("\n");
            break;
        }
        case OP_POP: {
            int n_pop = instr_get_A(inst);
            lulu_pop(self, n_pop);
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
    Chunk   chunk;
    cstring input;
} Comptime;

static void
compile_and_run(lulu_VM *self, void *userdata)
{
    Compiler  compiler;
    Comptime *ud = cast(Comptime *)userdata;

    compiler_init(self, &compiler);
    compiler_compile(&compiler, ud->input, &ud->chunk);

    self->chunk = &ud->chunk;
    self->ip    = self->chunk->code;
    vm_execute(self);
}

lulu_Status
vm_interpret(lulu_VM *self, cstring name, cstring input)
{
    Comptime ud;
    ud.input = input;
    chunk_init(&ud.chunk, name);
    lulu_Status status = vm_run_protected(self, &compile_and_run, &ud);
    chunk_free(self, &ud.chunk);
    return status;
}

void
vm_push(lulu_VM *self, const Value *value)
{
    lulu_check_stack(self, 1);
    *self->top = *value;
    self->top++;
}

lulu_Status
vm_run_protected(lulu_VM *self, Protected_Fn fn, void *userdata)
{
    Error_Handler handler;

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
vm_throw_error(lulu_VM *self, lulu_Status status)
{
    Error_Handler *handler = self->handlers;
    if (handler) {
        handler->status = status;
        LULU_IMPL_THROW(handler);
    } else {
        debug_fatal("Unexpected error. Aborting.");
        exit(EXIT_FAILURE);
    }
}

void
vm_comptime_error(lulu_VM *self, cstring file, int line, cstring msg, const char *where, int len)
{
    fprintf(stderr, "%s:%i: %s at '%.*s'\n", file, line, msg, len, where);
    vm_throw_error(self, LULU_ERROR_COMPTIME);
}

/**
 * @note 2024-12-30:
 *      This requires 'self->ip' to be correct, hence we cannot extract it into
 *      a local within 'vm_execute()'.
 */
void
vm_runtime_error(lulu_VM *self, cstring fmt, ...)
{
    va_list args;
    Chunk  *chunk = current_chunk(self);
    cstring file  = chunk->filename;
    int     line  = chunk->lines[self->ip - chunk->code - 1];
    va_start(args, fmt);
    fprintf(stderr, "%s:%i: ", file, line);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);

    // Ensure stack is valid for the next run.
    reset_stack(self);
    vm_throw_error(self, LULU_ERROR_RUNTIME);
}
