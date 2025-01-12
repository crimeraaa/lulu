package lulu

import "base:intrinsics"
import "core:mem"
import "core:fmt"
import "core:strings"

Object_Header :: struct {
    type    :  Value_Type,
    prev    : ^Object_Header,
}

OString :: struct {
    using header: Object_Header,
    len         : int,
    data        : [0]byte,
}

object_new :: proc($T: typeid, vm: ^VM, extra := 0) -> (typed_object: ^T)
where intrinsics.type_is_subtype_of(T, Object_Header) {

    ptr, _ := mem.alloc(size_of(T) + extra, align_of(T), vm.allocator)
    header := cast(^Object_Header)ptr
    when T == OString {
        header.type = .String
    } else {
        #panic("Invalid type!")
    }
    header.prev = vm.objects
    vm.objects  = header
    return cast(^T)header
}

object_free_all :: proc(vm: ^VM) {
    for object := vm.objects; object != nil; object = object.prev {
        #partial switch type := object.type; type {
        case .String:   ostring_free(vm, cast(^OString)object)
        case:           fmt.panicf("Cannot free a %v value!\n", type)
        }
    }
}

/*
Notes:
-   These strings are not compatible with C-style strings.
 */
ostring_new :: proc(vm: ^VM, input: string) -> (str: ^OString) {
    if prev, ok := vm.interned[input]; ok {
        return prev
    }
    n  := len(input)
    str = object_new(OString, vm, n + 1)
    defer vm.interned[input] = str

    str.len = n
    #no_bounds_check {
        copy(str.data[:n], input)
        str.data[n] = 0
    }
    return str
}

ostring_free :: proc(vm: ^VM, str: ^OString) {
    mem.free_with_size(str, size_of(str^) + str.len + 1, vm.allocator)
}

ostring_to_string :: proc(str: ^OString) -> string #no_bounds_check {
    return string(str.data[:str.len])
}

ostring_to_cstring :: proc(str: ^OString) -> cstring #no_bounds_check {
    assert(str.data[str.len] == 0)
    return cstring(cast([^]byte)&str.data)
}
