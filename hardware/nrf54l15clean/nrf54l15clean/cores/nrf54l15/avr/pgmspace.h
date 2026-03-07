#ifndef AVR_PGMSPACE_H
#define AVR_PGMSPACE_H

#include <stdint.h>
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif

typedef const void* PGM_VOID_P;
typedef const char* PGM_P;

#ifndef PSTR
#define PSTR(str_literal) (str_literal)
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr)     (*(const uint8_t*)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr)     (*(const uint16_t*)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr)    (*(const uint32_t*)(addr))
#endif
#ifndef pgm_read_float
#define pgm_read_float(addr)    (*(const float*)(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr)      (*(const void* const*)(addr))
#endif
#ifndef pgm_read_byte_near
#define pgm_read_byte_near(addr) pgm_read_byte(addr)
#endif
#ifndef pgm_read_word_near
#define pgm_read_word_near(addr) pgm_read_word(addr)
#endif
#ifndef pgm_read_dword_near
#define pgm_read_dword_near(addr) pgm_read_dword(addr)
#endif
#ifndef pgm_read_ptr_near
#define pgm_read_ptr_near(addr) pgm_read_ptr(addr)
#endif

#ifndef memcpy_P
#define memcpy_P(dst, src, len)    memcpy((dst), (src), (len))
#endif
#ifndef memcmp_P
#define memcmp_P(buf1, buf2, len)  memcmp((buf1), (buf2), (len))
#endif
#ifndef memchr_P
#define memchr_P(buf, ch, len)     memchr((buf), (ch), (len))
#endif
#ifndef strcpy_P
#define strcpy_P(dst, src)         strcpy((dst), (src))
#endif
#ifndef strncpy_P
#define strncpy_P(dst, src, len)   strncpy((dst), (src), (len))
#endif
#ifndef strcat_P
#define strcat_P(dst, src)         strcat((dst), (src))
#endif
#ifndef strncat_P
#define strncat_P(dst, src, len)   strncat((dst), (src), (len))
#endif
#ifndef strlen_P
#define strlen_P(src)              strlen((src))
#endif
#ifndef strcmp_P
#define strcmp_P(s1, s2)           strcmp((s1), (s2))
#endif
#ifndef strncmp_P
#define strncmp_P(s1, s2, len)     strncmp((s1), (s2), (len))
#endif
#ifndef strchr_P
#define strchr_P(s, c)             strchr((s), (c))
#endif
#ifndef strrchr_P
#define strrchr_P(s, c)            strrchr((s), (c))
#endif
#ifndef strstr_P
#define strstr_P(s1, s2)           strstr((s1), (s2))
#endif

#endif
