#include "../ctype.h"

int isdigit(int c) {
    return c >= '0' && c <= '9';
}

int islower(int c) {
    return c >= 'a' && c <= 'z';
}

int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

int isalpha(int c) {
    return islower(c) || isupper(c);
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

int isprint(int c) {
    return c >= 32 && c < 127;
}

int tolower(int c) {
    return isupper(c) ? ('a' + (c - 'A')) : c;
}

int toupper(int c) {
    return islower(c) ? ('A' + (c - 'a')) : c;
}
