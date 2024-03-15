-- TODO: Can we do what Lua does and determine argument count/return value count
--       at runtime somehow? This is particularly tricky because with function
--       calls that take in a function call which returns multiple values, we
--       can't rely on the compile-time argument count. e.g:
--
--       We also need to actually somehow determine the base pointer without
--       compile-time argument count which makes this all the more difficult.
--       We can't just go down the stack pointer until we hit the first function
--       because functions can be passed as arguments to other functions!
print("Hi mom!")
print("Hi", clock())
print(clock(), "Hi")
