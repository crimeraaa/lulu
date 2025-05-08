#+private
package lulu

import "base:intrinsics"
import "core:mem"
import "core:fmt"

Object_Header :: struct {
    type:  Value_Type       `fmt:"s"`,
    prev: ^Object_Header    `fmt:"p"`,
}

object_new :: proc($T: typeid, vm: ^VM, extra := 0) -> (typed_object: ^T)
where intrinsics.type_is_subtype_of(T, Object_Header) {

    ptr, err := mem.alloc(size_of(T) + extra, align_of(T), vm.allocator)
    // Assumes we are always in a protected call!
    if err != nil {
        vm_memory_error(vm)
    }

    header := cast(^Object_Header)ptr
    when T == OString {
        header.type = .String
    } else when T == Table {
        header.type = .Table
    } else {
        #panic("Invalid type!")
    }
    header.prev = vm.objects
    vm.objects  = header
    return cast(^T)header
}

/*
Notes:
-   If you're iterating, `object.prev` will be invalidated!
-   In that case save it beforehand.
 */
object_unlink :: proc(vm: ^VM, object: ^Object_Header) {
    vm.objects  = object.prev
    object.prev = nil
}

object_iterator :: proc(iter: ^^Object_Header) -> (object: ^Object_Header, ok: bool) {
    object = iter^
    if object == nil {
        return nil, false
    }

    iter^ = object.prev
    return object, true
}

object_free_all :: proc(vm: ^VM) {
    iter := vm.objects
    for object in object_iterator(&iter) {
        #partial switch type := object.type; type {
        case .String:
            object_unlink(vm, object)
            ostring_free(vm, cast(^OString)object)
        case .Table:
            table := cast(^Table)object
            object_unlink(vm, object)
            table_destroy(vm, table)
            mem.free(table)
        case:
            unreachable("Cannot free a %v value!", type)
        }
    }
}

objects_print_all :: proc(vm: ^VM) {
    fmt.println("=== OBJECTS: BEGIN ===")
    defer fmt.println("=== OBJECTS: END ===")

    iter := vm.objects
    for object in object_iterator(&iter) {
        #partial switch object.type {
        case .String:
            fmt.printfln("string: %q", ostring_to_string(cast(^OString)object))
        case .Table:
            fmt.printfln("table: %p", object)
        case: unreachable("Cannot print object type %v", object.type)
        }
    }
}
