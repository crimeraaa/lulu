-- Ensure we don't leak memory!
-- "hi" .. ' ' .. "mom!" == "hi mom!"
-- "hi" .. ' ' .. "mom!" == "hi" .. " mom!"
"hi" .. ' ' .. "mom" .. '!' == "hi" .. ' ' .. "mom" .. '!'
