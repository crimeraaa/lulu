-- Ensure we don't leak memory!
print "hi" .. ' ' .. "mom!" == "hi mom!"
print "hi" .. ' ' .. "mom!" == "hi" .. " mom!"
print("hi" .. ' ' .. "mom" .. '!' == "hi" .. ' ' .. "mom" .. '!')
