local cond = true
print("if-true:");
if cond then
    print("yay");
end;

print("if-false:")
cond = false
if cond then
    print("nay");
end;

print("if-true-else")
cond = true
if cond then
    print("yay");
else
    print("nay");
end;

print("if-false-else");
cond = not cond
if cond then
    print("yay");
else
    print("nay");
end;
