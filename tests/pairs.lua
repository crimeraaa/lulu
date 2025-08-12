local t = {
    'a', -- Don't use explicit index, cannot fold into OP_SET_ARRAY
    ['a'] = 97,
}

for k, v in pairs(t) do
    k = tostring(k)
    v = tostring(v)
    print(string.format("t[%s] = %s", k, v))
end
