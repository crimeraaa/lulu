if true then
    print("yay")
elseif 1 then -- unreachable since `if true` always runs.
    print(1)
elseif false then -- unreachable because it will never pass.
    print("nay")
else -- unreachable only based on context of the previous branches.
    print("huh?")
end
