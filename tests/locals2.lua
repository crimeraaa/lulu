local x, y = "x1", "y1"
print("x, y:", x, y, "Expected:", "x1", "y2")
do
    local x; -- If nothing happens in this block, then `startpc == endpc`
    print("x, y:", x, y, "Expected:", nil, "y1")
end

do
    local x = "x2(2)" -- this LOADK results in startpc != endpc
    print("x, y:", x, y, "Expected:", "x2(2)", "y1")
end
