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
    int line = -1;
    for (;;) {
        Token t = scan_token(&ls);
        Slice s{t.data->data, t.data->length};
        if (t.line != line) {
            std::printf("%4i ", t.line);
            line = t.line;
        } else {
            printf("   | ");
        }
        printf("%-12s %.*s\n", name_token(t.type), cast_int(s.length), s.string);

        if (t.type == Token::Type::Eof)
            break;
    }
    unused(name);
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
