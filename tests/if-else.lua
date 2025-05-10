local x = true
if x then
    print("success(1)");
end;

x = false
if x then
    print("wrong(1)");
end;

x = true
if x then
    print("success(2)");
else
    print("wrong(2)");
end;

x = false
if x then
    print("wrong(3)");
else
    print("success(3)");
end;
