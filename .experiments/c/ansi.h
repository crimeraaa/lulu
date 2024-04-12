#ifndef ANSI_ESCAPE_SEQUENCES_H
#define ANSI_ESCAPE_SEQUENCES_H

#define ESC     "\x1b"
#define CSI     ESC "["

#define SGR_RESET           0
#define SGR_BOLD            1
#define SGR_DIM             2
#define SGR_ITALIC          3
#define SGR_UNDERLINE       4
#define SGR_REVERSE         5
#define SGR_HIDE            6
#define SGR_STRIKETHROUGH   7

#define SGR_RESET           0
#define SGR_BLACK           30
#define SGR_RED             31
#define SGR_GREEN           32
#define SGR_YELLOW          33
#define SGR_BLUE            34
#define SGR_MAGENTA         35
#define SGR_CYAN            36
#define SGR_WHITE           37
#define SGR_DEFAULT         39

#define set_color(color)    CSI stringify(color) "m"
#define reset_colors(color) set_color(SGR_RESET)

#endif /* ANSI_ESCAPE_SEQUENCES_H */
