#include <stdio.h>
#include <string.h>

#define MAXLINE 256
#define PROMPT  "Enter: "

#ifdef _WIN32
#define HELP_EOF "CTRL + Z, then ENTER"
#else // _WIN32 not defined.
#define HELP_EOF "CTRL + D"
#endif // _WIN32

char *get_line(char *buffer, int len, const char *prompt) {
    printf("%s", prompt);
    if (!fgets(buffer, len, stdin)) {
        return NULL;
    }
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return buffer;
}

int main() {
    char buffer[MAXLINE];
    printf("(Hold " HELP_EOF " to exit)\n");
    while (get_line(buffer, sizeof(buffer), PROMPT)) {
        printf("\'%s\'\n", buffer);
    }
    return 0;
}
