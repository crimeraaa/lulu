#+private
package lulu

import "base:intrinsics"
import "core:mem"
import "core:fmt"

Object :: struct #raw_union {
    base:     Object_Base,
    ostring:  OString,
    table:    Table,
    function: Function,
}

Object_Base :: struct {
    next: ^Object `fmt:"p"`,
    type:  Type   `fmt:"s"`,
    flags: Object_Flags,
}

Object_Flags :: bit_set[Object_Flag; u8]

Object_Flag :: enum {
    Collectible,
}

object_new :: proc($T: typeid, vm: ^VM, extra := 0) -> ^Object
where intrinsics.type_is_subtype_of(T, Object_Base) {

    ptr, err := mem.alloc(size_of(T) + extra, align_of(T), vm.allocator)
    // Assumes we are always in a protected call!
    if err != nil {
        vm_memory_error(vm)
    }

    o := cast(^Object)ptr
    when T == OString {
        o.base.type = .String
    } else when T == Table {
        o.base.type = .Table
    } else when T == Function {
        o.base.type = .Function
    }else {
        #panic("Invalid type!")
    }
    return o
}

object_link :: proc(parent: ^^Object, o: ^Object) {
    o.base.next = parent^
    parent^     = o
}

/*
Notes:
-   If you're iterating, `o.prev` will be invalidated!
-   In that case save it beforehand.
 */
object_unlink :: proc(parent: ^^Object, o: ^Object) {
    parent^     = o.base.next
    o.base.next = nil
}

object_iterator :: proc(iter: ^^Object) -> (o: ^Object, ok: bool) {
    // Current iteration.
    o = iter^

    // Have we exhausted the linked list?
    ok = (o != nil)

    if ok {
        // Increment the iterator if there are still entries remaining.
        iter^ = o.base.next
    }
    return o, ok
}

object_free_all :: proc(vm: ^VM) {
    iter := vm.objects
    for o in object_iterator(&iter) {
        #partial switch o.base.type {
        case .Table:
            table_destroy(vm, &o.table)
            mem.free(o)
        case .Function:
            function_destroy(vm, &o.function)
            mem.free(o)
        case:
            unreachable("Cannot free a %v value!", o.base.type)
        }
    }
    vm.objects = nil
}

objects_print_all :: proc(vm: ^VM) {
    fmt.println("=== OBJECTS: BEGIN ===")
    defer fmt.println("=== OBJECTS: END ===")

    iter := vm.objects
    for o in object_iterator(&iter) {
        #partial switch o.base.type {
        case .String:
            fmt.printfln("string: %q", ostring_to_string(&o.ostring))
        case .Table:
            fmt.printfln("table: %p", o)
        case .Function:
            fmt.printfln("function: %p", o)
        case: unreachable("Cannot print object type %v", o.base.type)
        }
    }
}
