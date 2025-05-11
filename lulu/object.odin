#+private
package lulu

import "base:intrinsics"
import "core:mem"
import "core:fmt"

Object :: struct {
    type:  Value_Type  `fmt:"s"`,
    prev: ^Object      `fmt:"p"`,
}

object_new :: proc($T: typeid, vm: ^VM, extra := 0) -> ^T
where intrinsics.type_is_subtype_of(T, Object) {

    ptr, err := mem.alloc(size_of(T) + extra, align_of(T), vm.allocator)
    // Assumes we are always in a protected call!
    if err != nil {
        vm_memory_error(vm)
    }

    o := cast(^Object)ptr
    when T == OString {
        o.type = .String
    } else when T == Table {
        o.type = .Table
    } else {
        #panic("Invalid type!")
    }
    object_link(vm, o)
    return cast(^T)o
}

object_link :: proc(vm: ^VM, o: ^Object) {
    o.prev     = vm.objects
    vm.objects = o
}

/*
Notes:
-   If you're iterating, `o.prev` will be invalidated!
-   In that case save it beforehand.
 */
object_unlink :: proc(vm: ^VM, o: ^Object) {
    vm.objects  = o.prev
    o.prev      = nil
}

object_iterator :: proc(iter: ^^Object) -> (o: ^Object, ok: bool) {
    if o = iter^; o == nil {
        return nil, false
    }
    iter^ = o.prev
    return o, true
}

object_free_all :: proc(vm: ^VM) {
    iter := vm.objects
    for o in object_iterator(&iter) {
        #partial switch type := o.type; type {
        case .String:
            s := cast(^OString)o
            object_unlink(vm, o)
            ostring_free(vm, s)
        case .Table:
            t := cast(^Table)o
            object_unlink(vm, o)
            table_destroy(vm, t)
            mem.free(t)
        case:
            unreachable("Cannot free a %v value!", type)
        }
    }
}

objects_print_all :: proc(vm: ^VM) {
    fmt.println("=== OBJECTS: BEGIN ===")
    defer fmt.println("=== OBJECTS: END ===")

    iter := vm.objects
    for o in object_iterator(&iter) {
        #partial switch o.type {
        case .String:
            fmt.printfln("string: %q", ostring_to_string(cast(^OString)o))
        case .Table:
            fmt.printfln("table: %p", o)
        case: unreachable("Cannot print object type %v", o.type)
        }
    }
}
