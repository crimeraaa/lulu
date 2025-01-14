package lulu

import "base:intrinsics"
import "core:mem"
import "core:fmt"

Object_Header :: struct {
    type    :  Value_Type,
    prev    : ^Object_Header,
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

object_unlink :: proc(vm: ^VM, object: ^Object_Header) {
    vm.objects = object.prev
}

object_free_all :: proc(vm: ^VM) {
    for object := vm.objects; object != nil; object = object.prev {
        #partial switch type := object.type; type {
        case .String:
            object_unlink(vm, object)
            ostring_free(vm, cast(^OString)object)
        case .Table:
            object_unlink(vm, object)
            table_destroy(cast(^Table)object)
        case:
            fmt.panicf("Cannot free a %v value!\n", type)
        }
    }
}
