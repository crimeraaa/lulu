-- We should ignore the newline right after the "[[".
do
    local s = [[
    something
    something
]]

    print(s)
end

print([[Hi mom!]] == "Hi mom!")
print([[Hi\tmom!]] == "Hi\tmom!")
print([[Hi\tmom!]] == "Hi\\tmom!")
print([[Hi]] .. [[ ]] .. [[mom]] .. '!' == "Hi" .. ' ' .. "mom" .. '!')
print([[Hi]] .. '\t' .. [[mom]] .. '!' == "Hi\tmom!")
