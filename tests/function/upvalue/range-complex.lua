local function pyrange(start, stop, step)
    if not stop then
        start, stop = stop, 1
    end
    step = step or 1

    local function iter_fn(state, control)
        control = control + step
        if control < state then
            return control
        end
    end
    return --[[generator=]]iter_fn, --[[state=]]stop, --[[control=]]start - step
end --]==]

local t = {}
for i in pyrange(1, 4+1) do
    t[#t + 1] = function() print(i) end
end

for _, f in ipairs(t) do
    f()
end
