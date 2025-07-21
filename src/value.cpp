#include <stdio.h>

#include "value.hpp"
#include "object.hpp"

/**
 * @note(2025-07-21)
 *      Sync with `private.hpp:Value_Type`!
 */
const char *const
Value::type_names[] = {
    /* VALUE_NIL */             "nil",
    /* VALUE_BOOLEAN */         "boolean",
    /* VALUE_LIGHTUSERDATA */   "userdata",
    /* VALUE_NUMBER */          "number",
    /* VALUE_STRING */          "string",
    /* VALUE_TABLE */           "table",
    /* VALUE_FUNCTION */        "function",
};

bool
Value::operator==(const Value &b) const
{
    if (this->type() != b.type()) {
        return false;
    }

    switch (this->type()) {
    case VALUE_NIL:      return true;
    case VALUE_BOOLEAN:  return this->to_boolean() == b.to_boolean();
    case VALUE_NUMBER:   return lulu_Number_eq(this->to_number(), b.to_number());
    case VALUE_LIGHTUSERDATA: return this->to_userdata() == b.to_userdata();
    case VALUE_STRING:   // fall-through
    case VALUE_TABLE:
    case VALUE_FUNCTION: return this->to_object() == b.to_object();
    case VALUE_INTEGER:
    case VALUE_CHUNK:
        break;
    }
    lulu_unreachable();
}

void
value_print(const Value &v)
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
    case VALUE_FUNCTION: {
        goto print_pointer;
    }
    case VALUE_INTEGER:
    case VALUE_CHUNK:
        lulu_unreachable();
        break;
    }
}
