function printf(fmt, ...)
    local msg = string.format(fmt, ...)
    io.write(msg)
end

printf("Hi %s!\n", "mom")
