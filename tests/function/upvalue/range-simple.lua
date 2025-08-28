local t = {}

---@diagnostic disable: undefined-global

for i in range(1, 4+1) do
    t[#t + 1] = function() print(i) end
end

for _, f in ipairs(t) do
    f()
end
