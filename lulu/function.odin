package lulu

Function :: struct {
    using base: Object_Base,
    chunk:      Chunk,
    arity:      int,
}

function_new :: proc(vm: ^VM, source: string) -> ^Function {
    o := object_new(Function, vm)
    object_link(&vm.objects, o)

    f := &o.function
    chunk_init(&f.chunk, source)
    return f
}

function_destroy :: proc(vm: ^VM, f: ^Function) {
    chunk_destroy(vm, &f.chunk)
}
