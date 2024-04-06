#!/usr/bin/env lua

require "slice"

---@brief Create one giant line for use on the command-line.
---@param cmd     string    Program to be executed from the command-line.
---@param flags   string[]  Command-line flags to said program.
---@param pargs   string[]? Positional arguments, if any, for said program.
---@param default string[]? Default positional arguments if none given.
local function make_cmd(cmd, flags, pargs, default)
    local t = {cmd, table.concat(flags, ' ')}
    
    -- If no pargs given and this command has defaults, use them instead.
    if (not pargs or #pargs == 0) and default then
        pargs = default
    end

    -- Don't append '--' for the interpreter as it can't handle them.
    if cmd ~= INTERPRETER and #pargs >= 1 then
        t[#t + 1] = "--"
    end

    -- Don't append a positional argument list if we don't have any.
    if #pargs >= 1 then
        t[#t + 1] = table.concat(pargs, ' ')
    end
    return table.concat(t, ' ')
end

local function execute_command(self, pargs)
    local cmd = make_cmd(self.cmd, self.flags, pargs, self.default)
    io.stdout:write("[COMMAND]:\n", cmd, '\n')
    io.stdout:write("[OUTPUT]:\n")
    return os.execute(cmd) -- Bad practice but I'm lazy :)
end

local function capture_command(self, pargs)
    local cmd = make_cmd(self.cmd, self.flags, pargs, self.default)
    local out = io.popen(cmd, "r"):read("*a")
    return cmd, out
end

local function surround(subject, delimiter)
    return string.format("%s%s%s", delimiter, subject, delimiter)
end

local function squote(subject)
    return surround(subject, '\'')
end

local function dquote(subject)
    return surround(subject, '\"')
end

local function unsurround(subject, delimiter)
    local pattern = string.format("^%s+(.*)%s+$", delimiter, delimiter)
    return string.match(subject, delimiter)
end

INTERPRETER = "./bin/lulu"
OPTIONS = {
    ["help"] = {
        cmd = arg[0] .. " help",
        flags = nil,
        help = "List help for each item in [command...] else print all help.",
        usage = "[command...]",
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
                    io.stdout:write(key, '\n')
                    io.stdout:write('\t')
                    if opt.cmd then
                        io.stdout:write(opt.cmd, ' ')
                    end
                    if opt.flags then
                        io.stdout:write(table.concat(opt.flags, ' '), ' ')
                    end
                    io.stdout:write(opt.usage, '\n')
                    io.stdout:write('\t', opt.help, '\n')
                end
            end
            return 0
        end,
    },
    ["run"] = {
        cmd  = INTERPRETER,
        flags = nil,
        help = "If no pargs, run " .. squote(INTERPRETER) .. " else run [script]",
        usage = "[script]",
        call = execute_command,
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
        usage = "<file...>",
        call = function(self, pargs)
            assert(#pargs > 0, "'disassemble' requires at least 1 file")
            return execute_command(self, pargs)
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
        help = "Print name list (symbol table) for compiled files.",
        flags = {"--demangle",
                "--format=bsd", -- I prefer one of bsd or sysv
                "--radix=x",    -- 'd':decimal, 'o':octal, 'x':hexadecimal
                "--numeric-sort",
                "--with-symbol-versions"},
        usage = "<file...>",
        call = function(self, pargs)
            assert(#pargs > 0, "'names' requires at least 1 file")
            return execute_command(self, pargs)
        end,
    },
    ["memcheck"] = {
        cmd  = "valgrind",
        flags = {"--leak-check=full", 
                "--track-origins=yes", 
                "2>&1"}, -- valgrind writes to stdout by default so redirect.
        help = "Run Valgrind memcheck with some helpful defaults.",
        usage = "[exe [args...]]",
        default = {INTERPRETER},
        call = execute_command,
    },
    ["search"] = {
        cmd = "grep",
        flags = {"--line-number",
                "--directories=recurse",
                "--include=*.c",
                "--include=*.h",
                "--color=always"},
        help = "Find all occurences of POSIX `<regex>` in `directory`/ies.",
        default = {"./src/"},
        usage = "<regex> [directory...]",
        call = function(self, pargs)
            assert(#pargs >= 1, "Requires at least a regular expression")
            -- Remove regex from pargs and append to flags
            self.flags[#self.flags + 1] = dquote(table.remove(pargs, 1))
            return execute_command(self, pargs)
        end
    },
    ["whitespace"] = {
        cmd = "grep",
        flags = {"--color=never",
                "--files-with-matches",
                "--directories=recurse",
                "\"[[:space:]]\\+$\""}, -- match trailing whitespaces per line
        default = {"./src/"},
        help = "For each `directory` (or `./src/`), list files with trailing whitespaces.",
        usage = "[directory...]",
        call = execute_command,
    },
    ["replace"] = {
        cmd = "sed",
        flags = {"--in-place"},
        help = "Replace POSIX `regex` with `substitution` in [file...] or `./src/`.",
        usage = "<regex> <substitution> [file...]",
        default = {"./src/*"},
        call = function(self, pargs)
            assert(#pargs >= 3, "Requires <regex>, <substitution> and 1+ file/s")
            -- Remove regex and replacement pattern from pargs
            local pat = table.remove(pargs, 1)
            local sub = table.remove(pargs, 1) -- Shifted down to index 1
            local command = string.format("--expression=\"s/%s/%s/g\"", pat, sub)
            self.flags[#self.flags + 1] = command
            return execute_command(self, pargs)
        end,
    },
    ["fix-whitespace"] = {
        cmd = nil,
        flags = nil,
        help = "Remove all trailing whitespaces from each file in [directory...] or `./src/`.",
        usage = "[directory]...",
        default = {"./src/"},
        call = function(self, pargs)
            if #pargs == 0 then
                local choices = {y = true, yes = true, n = false, no = false}
                io.stdout:write("WARNING: This will find-and-replace everything in `./src/`.\n",
                    "Do you want to continue? (y/n): ")
                local confirm = io.stdin:read("*l")
                if choices[string.lower(confirm)] then
                    io.stdout:write("Continuing...\n")
                else
                    io.stdout:write("Aborting.\n")
                    return 1
                end
            end

            local search  = OPTIONS["whitespace"]
            local replace = OPTIONS["replace"]
            local _, list = capture_command(search, pargs)
            list = string.split(list, '\n')
            
            if #list == 0 then
                io.stdout:write("No files with trailing whitespace found.\n")
                return 1
            end
            
            -- Prepend regex and substitution due to how replace works
            -- We need to remove the quotes from the regex as well.
            table.insert(list, 1, search.flags[#search.flags]:sub(2, -2))
            table.insert(list, 2, "")
            
            return replace:call(list)
        end,
    }
}

PARGS_NOTE = {
    "'pargs' means 'positional arguments' which usually come after a '--'.",
    "Usually it is a variadic argument list, e.g. `ls -- src obj bin`.",
}

local function main(argc, argv)
    -- argc was adjusted to reflect the presence of argv[0].
    if argc == 1 then
        local help = OPTIONS["help"]
        io.stdout:write("[USAGE]:\n\t", argv[0], " <option> [pargs...]\n")
        io.stdout:write("[NOTE]:\n\t", table.concat(PARGS_NOTE, "\n\t"), '\n')
        help:call(nil)
        os.exit(2)
    end

    local opt = OPTIONS[argv[1]]
    if not opt then
        io.stdout:write("[ERROR]:\nUnknown option '", argv[1], "'.\n")
        os.exit(2)
    end
    -- Positional argument list to argv[1] which is the option.
    local pargs = table.slice(argv, 2, argc)
    local res = opt:call(pargs)
    io.stdout:write("[RETURN]:\n", res, "\n")
end

main(#arg + 1, arg)
