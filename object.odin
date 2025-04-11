package lulu

import "base:intrinsics"
import "core:mem"
import "core:fmt"

Object_Header :: struct {
    type:  Value_Type,
    prev: ^Object_Header,
}

object_new :: proc($T: typeid, vm: ^VM, extra := 0) -> (typed_object: ^T)
where intrinsics.type_is_subtype_of(T, Object_Header) {

    ptr, _ := mem.alloc(size_of(T) + extra, align_of(T), vm.allocator)
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
    if object == nil do return nil, false

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
            object_unlink(vm, object)
            table_destroy(vm, cast(^Table)object)
        case:
            fmt.panicf("Cannot free a %v value!", type)
        }
    }
}
