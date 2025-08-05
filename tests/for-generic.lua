for k, v in pairs(_G) do
    print(string.format("_G[%q] = %s", tostring(k), tostring(v)))
end
