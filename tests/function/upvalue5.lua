local closures = {}

-- Each closure captures a unique instance of 'i', consistent with
-- the semantics of Lua.
for i = 1, 4 do
    local function f()
        print(i)
    end
    closures[#closures + 1] = f
end

-- 1 2 3 4
for _, closure in ipairs(closures) do
    closure()
end
