local x; -- nil
print(x and x >= 1 or 13) -- fallback to 13
print(13 or x and x >= 1) -- truthy lhs of 'or' prioritized over eval of 'and'
local y=1;
print(y and y >= 1 or 42) -- evaluate y >= 1
print(42 or y and y >= 1) -- again truthy lhs is given priority
-- grouping tests
print((x and x >= 1) or 13) -- fallback to 13 since lhs is falsy
print(x and (x >= 1 or 13)) -- falsy lhs of 'and' prioritized over eval of 'or'
print(x >= 1)
