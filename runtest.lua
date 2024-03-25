#!/usr/local/bin/lua

require "slice"

---@brief Create one giant line for use on the command-line.
---@param cmd  string      Program to be executed from the command-line.
---@param flags string[]    Command-line flags to said program.
---@param pargs string[]    Positional arguments, if any, for said program.
local function make_cmd(cmd, flags, pargs)
    local t = {cmd, table.concat(flags, ' ')}
    -- Don't append a positional argument list if we don't have any.
    if #pargs >= 1 then
        t[#t + 1] = "--"
        t[#t + 1] = table.concat(pargs, ' ')
    end
    return table.concat(t, ' ')
end

local function basic_call(self, pargs)
    local cmd = make_cmd(self.cmd, self.flags, pargs)
    local out = io.popen(cmd, 'r'):read("*a") -- *a reads entire contents
    return cmd, out
end

INTERPRETER = "./bin/lulu"
OPTIONS = {
    ["help"] = {
        cmd = arg[0] .. " help",
        flags = {},
        help = "No pargs lists all options, otherwise gets help for each.",
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
                    local info = string.format("\t%-16s", key)
                    io.stderr:write(info, opt.help, '\n')
                end
            end
            os.exit(0) -- End usage here as otherwise we'd print output
        end,
    },
    ["run"] = {
        cmd  = INTERPRETER,
        flags = {},
        help = "No pargs runs " .. INTERPRETER .. ", else runs each script (left-right)",
        call = basic_call,
    },
    ["disassemble"] = {
        cmd = "objdump",
        flags = {"--disassemble",
                "--disassembler-color=on", -- Can't use terminal or extended here
                "-Mintel", -- Use Intel assembly syntax instead of AT&T
                "--source",
                "--source-comment",
                "--wide",
                "--visualize-jumps=color"},
        help = "Commented Intel-style assembly for given pargs.",
        call = function(self, pargs)
            assert(#pargs > 0, "'disassemble' requires at least 1 parg")
            return basic_call(self, pargs)
        end
    },
    ["memcheck"] = {
        cmd  = "valgrind",
        flags = {"--leak-check=full", 
                "--track-origins=yes", 
                "2>&1"}, -- valgrind writes to stderr by default so redirect.
        help = "No pargs runs " .. INTERPRETER .. ", else puts pargs after '--'.",
        call = function(self, pargs)
            if #pargs == 0 then
                pargs = {INTERPRETER}
            end
            return basic_call(self, pargs)
        end
    },
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
