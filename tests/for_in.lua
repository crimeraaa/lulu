function lua_ipairs_aux(t, control)
    control = control + 1
    if control <= #t then return control, t[control] end
end

function lua_ipairs(t)
    return --[[generator=]]lua_ipairs_aux, --[[state=]]t, 0
end

_G[1] = "Hi"
_G[2] = "mom"

for i, v in lua_ipairs(_G) do
    print(i, v)
end
