-- We should ignore the newline right after the "[[".
do
    local s = [[
    something
    something
]]

    print(s)
end

print("Expect: true", [[Hi mom!]] == "Hi mom!")
print("Expect: false", [[Hi\tmom!]] == "Hi\tmom!")
print("Expect: true", [[Hi\tmom!]] == "Hi\\tmom!")
print("Expect: true", [[Hi]] .. [[ ]] .. [[mom]] .. '!' == "Hi" .. ' ' .. "mom" .. '!')
print("Expect: true", [[Hi]] .. '\t' .. [[mom]] .. '!' == "Hi\tmom!")
