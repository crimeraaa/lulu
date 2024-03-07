-- Comment out various parts to see if our error handling is ok.
-- local cond=false
-- if cond then
--     print("yay")
-- elseif not cond then
--     print("nay")
-- else
--     print("huh???")
-- end

-- Arithmetic errors
local x=0
local y
print(y and y <= x or "nope")
