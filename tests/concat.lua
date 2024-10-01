-- Ensure we don't leak memory!
print("---CONCAT TEST---")
print("hi" .. ' ' .. "mom!" == "hi mom!")
print("hi" .. ' ' .. "mom!" == "hi" .. " mom!")
print("hi" .. ' ' .. "mom" .. '!' == "hi" .. ' ' .. "mom" .. '!')

print("---OPARG1 FOLDING TEST---")
print("Hi" .. (' ' .. ("mom" .. ('!'))))
