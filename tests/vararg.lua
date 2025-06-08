-- main function has varargs; they refer to the positional arguments passed on
-- the command-line.
print(arg[0], unpack(arg))
print(...)
