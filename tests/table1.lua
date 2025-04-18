--[[ 
stack:
    [0]: {}
--]]
local t = {}

--[[ 
stack:
    [1]: {}
    [0]: {}

state:
reg[0]["k"] = reg[1]
--]]
t.k = {}

--[[ 
stack:
    reg[2]: 13
    reg[1]: reg[0]["k"] = {}
    reg[0]: {}

state:
    reg[0]["k"]["v"] = reg[2]
]]
t.k.v = 13
-- print("Expected:", "table: 0x...", "Actual:", t)
-- print("Expected:", "table: 0x...", "Actual:", t.k)
-- print("Expected:", 13, "Actual:", t.k.v)
