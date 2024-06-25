file ./bin/main

lay src
set print pretty on

# break main.cpp:load_buffer
# break main.cpp:load
# break main.cpp:parse
break lexer.cpp:scan_token

run
