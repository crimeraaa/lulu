package lulu

Function :: struct {
    using base: Object_Base,
    using data: struct #raw_union {
        using lua: Function_Lua,
        odin:      Function_Odin,
    },
    is_lua: bool,
}

Function_Lua :: struct {
    chunk: Chunk,
    arity: int,
}

Function_Odin :: #type proc(vm: ^VM, n_arg: int) -> (n_ret: int)

function_new :: proc {
    function_lua_new,
    function_odin_new,
}

function_lua_new :: proc(vm: ^VM, source: string) -> ^Function {
    o := object_new(Function, vm)
    object_link(&vm.objects, o)

    f := &o.function
    f.is_lua = true
    chunk_init(&f.chunk, source)
    return f
}

function_odin_new :: proc(vm: ^VM, p: Function_Odin) -> ^Function {
    o := object_new(Function, vm)
    object_link(&vm.objects, o)

    f := &o.function
    f.is_lua = false
    f.odin   = p
    return f
}

function_destroy :: proc(vm: ^VM, f: ^Function) {
    if f.is_lua {
        chunk_destroy(vm, &f.chunk)
    }
}
