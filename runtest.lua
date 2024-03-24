#!/usr/local/bin/lua

require "slice"

local function make_cmd(name, flags, pargs)
    local t = {name, table.concat(flags, ' ')}
    -- Don't append a positional argument list if we don't have any.
    if #pargs >= 1 then
        t[#t + 1] = "--"
        t[#t + 1] = table.concat(pargs, ' ')
    end
    return table.concat(t, ' ')
end

INTERPRETER = "./bin/lulu"
OPTIONS = {
    ["help"] = {
        name = arg[0] .. " help",
        flags = {},
        help = "Get detailed information for 1 or more specified options.",
        call = function(self, pargs)
            io.stderr:write("[OPTIONS]:\n")
            if (type(pargs) ~= "table") or (#pargs == 0) then
                pargs = table.array_of_keys(OPTIONS)
            end
            for _, key in ipairs(pargs) do
                local opt = OPTIONS[key]
                if not opt then
                    io.stderr:write("Unknown option '", tostring(key), "'.\n")
                else
                    io.stderr:write(key, ":\tRuns '", opt.name, "'. ", opt.help, '\n')
                end
            end
            os.exit(0)
        end,
    },
    ["run"] = {
        name = INTERPRETER,
        flags = {},
        help = "No arguments runs REPL, otherwise runs [pargs..] scripts.",
        call = function(self, pargs)
            local cmd = make_cmd(self.name, self.flags, pargs)
            local output = io.popen(cmd, 'r'):read("*a")
            return cmd, output
        end,
    }
}

PARGS_NOTE = [[
- 'pargs' means 'positional arguments' which usually come after a '--'.
- Usually it is a variadic argument list, e.g. `ls -- src obj bin`.]]

local function main(argc, argv)
    -- argc was adjusted to reflect the presence of argv[0].
    if argc == 1 then
        io.stderr:write("[USAGE]:\n", argv[0], " <option> [pargs..]\n")
        io.stderr:write("[NOTE]:\n", PARGS_NOTE, '\n')
        OPTIONS["help"]:call(nil)
        os.exit(2)
    end
    local opt = OPTIONS[argv[1]]
    if not opt then
        io.stderr:write("[ERROR]:\nUnknown option '", argv[1], "'.\n")
        os.exit(2)
    end
    -- Positional argument list to argv[1] which is the option.
    local pargs = table.slice(argv, 2, argc)
    local cmd, output = opt:call(pargs)
    io.stderr:write("[COMMAND]:\n", cmd, "\n")
    io.stderr:write("[OUTPUT]:\n", output, "\n")
end

main(#arg + 1, arg)
