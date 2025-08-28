#include <stdio.h>

#include "object.hpp"
#include "value.hpp"

/**
 * @note(2025-07-21)
 *      ORDER: Sync with `private.hpp:Value_Type`!
 */
const char *const Value::type_names[] = {
    "nil",      // VALUE_NIL
    "boolean",  // VALUE_BOOLEAN
    "userdata", // VALUE_LIGHTUSERDATA
    "number",   // VALUE_NUMBER
    "string",   // VALUE_STRING
    "table",    // VALUE_TABLE
    "function", // VALUE_FUNCTION
    "chunk",    // VALUE_CHUNK
    "upvalue",  // VALUE_UPVALUE
    "integer",  // VALUE_INTEGER
};

bool
Value::operator==(Value b) const
{
    if (this->type() != b.type()) {
        return false;
    }

    switch (this->type()) {
    case VALUE_NIL:
        return true;
    case VALUE_BOOLEAN:
        return this->to_boolean() == b.to_boolean();
    case VALUE_NUMBER:
        return lulu_Number_eq(this->to_number(), b.to_number());
    case VALUE_LIGHTUSERDATA:
        return this->to_userdata() == b.to_userdata();
    case VALUE_STRING:
    case VALUE_TABLE:
    case VALUE_FUNCTION:
        return this->to_object() == b.to_object();
    case VALUE_INTEGER:
    case VALUE_CHUNK:
    case VALUE_UPVALUE:
        break;
    }
    lulu_panicf("Value_Type(%i) should not reach here", this->type());
    lulu_unreachable();
}

void
value_print(Value v)
{
    Value_Type t = v.type();
    switch (t) {
    case VALUE_NIL:
        fputs("nil", stdout);
        break;
    case VALUE_BOOLEAN:
        fputs(v.to_boolean() ? "true" : "false", stdout);
        break;
    case VALUE_LIGHTUSERDATA:
print_pointer:
        fprintf(stdout, "%s: %p", Value::type_names[t], v.to_pointer());
        break;
    case VALUE_NUMBER:
        fprintf(stdout, LULU_NUMBER_FMT, v.to_number());
        break;
    case VALUE_STRING: {
        OString *s = v.to_ostring();
        char     q = (s->len == 1) ? '\'' : '\"';
        fprintf(stdout, "%c%s%c", q, s->data, q);
        break;
    }
    case VALUE_TABLE:
    case VALUE_FUNCTION:
        goto print_pointer;
    case VALUE_INTEGER:
    case VALUE_CHUNK:
    case VALUE_UPVALUE:
        lulu_panicf("Value_Type(%i) should not reach here", t);
        lulu_unreachable();
        break;
    }
}
