#include "conf.hpp"
#include "io.hpp"
#include "mem.hpp"
#include "lexer.hpp"
#include "global.hpp"

// https://www.lua.org/source/5.1/lparser.c.html#luaY_parser
static void parse(Global *g, Stream *z, Buffer *b, const char *name)
{
    Lexer ls;
    init_lexer(&ls, g, z, b);
    unused(name);
    int line = -1;
    for (;;) {
        Token t = scan_token(&ls);
        if (t.line != line) {
            std::printf("%4i ", t.line);
            line = t.line;
        } else {
            std::printf("   | ");
        }
        
        std::printf("%-12s ", name_token(t.type));
        switch (t.type) {
        case Token::Type::Identifier:
        case Token::Type::String:
        case Token::Type::Error:
            std::printf("%s", t.data.string->data);
            break;
        case Token::Type::Number:
            std::printf("%.14g", t.data.number);
            break;
        default:
            std::printf("%s", display_token(t.type));
            break;
        }
        std::printf("\n");

        if (t.type == Token::Type::Eof)
            break;
    }
}

static void load(Global *g, Reader *rd, const char *name)
{
    Stream z;
    Buffer b;
    if (name == nullptr)
        name = "?";
    init_stream(&z, rd->readfn, rd->context);
    init_buffer(&b);
    parse(g, &z, &b, name);
    free_buffer(g, &b);
}

// https://www.lua.org/source/5.1/lauxlib.c.html#getS
static const char *read_string(size_t *out, void *ctx)
{
    Slice *s = cast<Slice*>(ctx);
    // Was read already?
    if (s->length == 0)
        return nullptr;
    // Mark as read for next time.
    *out = s->length;
    s->length = 0;
    return s->string;
}

// https://www.lua.org/source/5.1/lauxlib.c.html#luaL_loadbuffer
static void load_buffer(Global *g, const char *buf, size_t len, const char *name)
{
    // Context for our reader.
    Slice  s{buf, len};
    Reader rd;
    init_reader(&rd, &read_string, cast<void*>(&s));
    load(g, &rd, name);
}

int main(int argc, const char *argv[])
{
    Global g;
    char   input[MAX_INPUT] = {0};

    unused2(argc, argv);
    init_global(&g);
    for (;;) {
        // Ensure prompt is printed
        std::fputs("> ", stdout);
        std::fflush(stdout);

        // UNIX: CTRL + D, Windows: CTRL + Z + ENTER
        if (!std::fgets(input, sizeof(input), stdin)) {
            std::fputc('\n', stdout);
            break;
        }
        load_buffer(&g, input, std::strlen(input), "=stdin");
    }
    free_global(&g);
    return 0;
}
