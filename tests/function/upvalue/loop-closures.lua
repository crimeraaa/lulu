local closures = {}

-- Each closure captures a unique instance of 'i', consistent with
-- the semantics of Lua.
for i = 1, 4 do
    closures[#closures + 1] = function() print(i) end
end

-- for i = 1, #closures do
--     closures[i]()
-- end

-- 1 2 3 4
for _, f in ipairs(closures) do
    f()
end
