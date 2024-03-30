#!/usr/bin/env lua

require "slice"

---@brief Create one giant line for use on the command-line.
---@param cmd  string       Program to be executed from the command-line.
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

local function quote(s)
    return string.format("'%s'", s)
end

INTERPRETER = "./bin/lulu"
OPTIONS = {
    ["help"] = {
        cmd = arg[0] .. " help",
        flags = {},
        help = "No pargs lists all options, otherwise gets help for each.",
        call = function(self, pargs)
            io.stdout:write("[OPTIONS]:\n")
            if (not pargs or #pargs == 0) then
                pargs = table.array_of_keys(OPTIONS)
            end
            for _, key in ipairs(pargs) do
                local opt = OPTIONS[key]
                if not opt then
                    io.stdout:write("Unknown option '", tostring(key), "'.\n")
                else
                    local info = string.format("\t%-16s", key)
                    io.stdout:write(info, opt.help, '\n')
                end
            end
            os.exit(0) -- End usage here as otherwise we'd print output
        end,
    },
    ["run"] = {
        cmd  = INTERPRETER,
        flags = {},
        help = "No pargs runs " .. quote(INTERPRETER) .. ", else runs each script (left-right)",
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
    --  From `man nm`:
    --  A   - Value is absolute: will not be changed by further linking.
    --  B|b - In BSS data section (0-initialized/unitialized).
    --  C|c - Common symbols (uninitialized data), can occur multiple times.
    --      - If defined anywhere common symbols are treated as undefined
    --      - references. Lowercase c is in a special section for small commons.
    --  D|d - In the initialized data section.
    --  G|g - In the initialized data section for small objects.
    --  i   - PE: Symbol is in implementation-specific section of DLL.
    --      - ELF: Is an indirect function (GNU extension).
    --  I   - Indirect reference to another symbol.
    --  N   - Debugging symbol.
    --  n   - Read-only data section.
    --  p   - Stack unwind section.
    --  R|r - Read-only data section. Lowercase may mean static.
    --  S|s - Uninitialized/0-initialized data section for small objects.
    --  T|t - Text (code) section. Lowercase may mean static.
    --  U   - Undefined.
    --  u   - Unique lglobal symbol (GNU Extension).
    --  V|v - Weak object.
    --  W|w - Weak symbol
    ["names"] = {
        cmd = "nm",
        help = "Print name list (symbol table) for given pargs.",
        flags = {"--demangle",
                "--format=bsd", -- I prefer one of bsd or sysv
                "--radix=x",    -- 'd':decimal, 'o':octal, 'x':hexadecimal
                "--numeric-sort",
                "--with-symbol-versions"},
        call = function(self, pargs)
            assert(#pargs > 0, "'names' requires at least 1 parg.")
            return basic_call(self, pargs)
        end,
    },
    ["memcheck"] = {
        cmd  = "valgrind",
        flags = {"--leak-check=full", 
                "--track-origins=yes", 
                "2>&1"}, -- valgrind writes to stdout by default so redirect.
        help = "No pargs runs " .. quote(INTERPRETER) .. ", else puts pargs after '--'.",
        call = function(self, pargs)
            if #pargs == 0 then
                print("(enter input, prompt is hidden)")
                pargs = {INTERPRETER}
            end
            return basic_call(self, pargs)
        end
    },
    ["whitespace"] = {
        cmd = "grep",
        flags = {"--perl-regexp", -- \s is a perl thing
                "--line-number", 
                "--files-with-matches", 
                "\"\\s+$\""}, -- match trailing whitespaces per line
        help = "List all the source files that have trailing whitespaces.",
        call = function(self, pargs)
            -- No args so resort to checking only the files in `./src/`.
            if #pargs == 0 then
                local ls = io.popen("readlink --canonicalize -- ./src/*"):read("*a")
                pargs = string.split(ls, "\n")
            end
            return basic_call(self, pargs)
        end
    },
}

PARGS_NOTE = {
    "'pargs' means 'positional arguments' which usually come after a '--'.",
    "Usually it is a variadic argument list, e.g. `ls -- src obj bin`.",
}

local function main(argc, argv)
    -- argc was adjusted to reflect the presence of argv[0].
    if argc == 1 then
        io.stdout:write("[USAGE]:\n\t", argv[0], " <option> [pargs..]\n")
        io.stdout:write("[NOTE]:\n\t", table.concat(PARGS_NOTE, "\n\t"), '\n')
        OPTIONS["help"]:call(nil)
        os.exit(2)
    end
    local opt = OPTIONS[argv[1]]
    if not opt then
        io.stdout:write("[ERROR]:\nUnknown option '", argv[1], "'.\n")
        os.exit(2)
    end
    -- Positional argument list to argv[1] which is the option.
    local pargs = table.slice(argv, 2, argc)
    local cmd, output = opt:call(pargs)
    io.stdout:write("[COMMAND]:\n", cmd, "\n")
    io.stdout:write("[OUTPUT]:\n", output, "\n")
end

main(#arg + 1, arg)
