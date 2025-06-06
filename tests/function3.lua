function greet()
    print("Hi mom!")
end

greet()                    -- stack: [ greet, "hi mom" ]
greet("hai", "mom", "lol") -- stack: [ greet, "hai", "mom", "lol" ]
print("bruh")              -- stack: [ "bruh" ]
