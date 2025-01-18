-- Ensure we don't leak memory!

-- interned: {"hi", ' ', "mom!", "hi mom!"} (+4)
print("hi" .. ' ' .. "mom!" == "hi mom!")

-- interned: {" mom!"} (+1)
print("hi" .. ' ' .. "mom!" == "hi" .. " mom!")

-- interned: {'!', "mom"} (+2)
print("hi" .. ' ' .. "mom" .. '!' == "hi" .. ' ' .. "mom" .. '!')

-- interned: {"yo"} (+1)
print("yo")
print("folding test")
-- print("Hi" .. (' ' .. ("mom" .. ('!'))))
