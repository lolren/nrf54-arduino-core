/*
 * Arduino WCharacter - Character classification and conversion
 *
 * Licensed under the Apache License 2.0
 */

#ifndef WCharacter_h
#define WCharacter_h

#include <ctype.h>

inline boolean isAlphaNumeric(int c) { return (isalnum(c)); }
inline boolean isAlpha(int c) { return (isalpha(c)); }
inline boolean isAscii(int c) { return ((unsigned int)c <= 0x7F); }
inline boolean isWhitespace(int c) { return (isspace(c)); }
inline boolean isControl(int c) { return (iscntrl(c)); }
inline boolean isDigit(int c) { return (isdigit(c)); }
inline boolean isGraph(int c) { return (isgraph(c)); }
inline boolean isLowerCase(int c) { return (islower(c)); }
inline boolean isPrintable(int c) { return (isprint(c)); }
inline boolean isPunct(int c) { return (ispunct(c)); }
inline boolean isSpace(int c) { return (isspace(c)); }
inline boolean isUpperCase(int c) { return (isupper(c)); }
inline boolean isHexadecimalDigit(int c) { return (isxdigit(c)); }
inline int toAscii(int c) { return (c & 0x7F); }
inline int toLowerCase(int c) { return (tolower(c)); }
inline int toUpperCase(int c) { return (toupper(c)); }

#endif
