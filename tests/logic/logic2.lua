local print = print

---@type any, any
local x, y = true, false

-- logical `and` short-circuits when the left side is falsy.
-- truthy and falsy -> falsy
-- truthy or  falsy -> truthy
print("x = true and y = false:", x and y, "expected:", false)
print("x = true or y = false:", x or y, "expected:", true)

-- truthy and falsy -> falsy
-- truthy or falsy -> truthy
y = nil
print("x = true and y = nil:", x and y, "expected:", nil)
print("x = true or y = nil", x or y, "expected:", true)

-- truthy and truthy -> second truthy
-- truthy or truthy -> first truthy
y = 1
print("x = true and y = 1", x and y, "expected:", 1)
print("x = true or y = 1", x or y, "expected:", true)

-- truthy and truthy -> second truthy
-- truthy or truthy -> first truthy
x, y = 9, 10
print("x = 9 and y = 10", x and y, "expected:", 10)
print("x = 9 or y = 10", x or y, "expected:", 9)

-- falsy and falsy -> first falsy
-- falsy or falsy -> second falsy
x, y = false, nil
print("x = false and y = nil", x and y, "expected:", false)
print("x = false or y = nil", x or y, "expected:", nil)

-- falsy and true
y = true
print("x = false and y = true", x and y, "expected:", false)

-- falsy and truthy
y = 1
print("x = false and y = 1", x and y, "expected:", false)

x, y = nil, true
print("x = nil and y = true", x and y, "expected:", nil)
