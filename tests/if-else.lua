local x = true
if x then
    print("success(1)");
end;

x = false
if x then
    print("wrong(2)");
end;

x = true
if x then
    print("success(3)");
else
    print("wrong(3)");
end;

x = false
if x then
    print("wrong(4)");
else
    print("success(4)");
end;
