/*
 * Arduino Stream - base class for character-based streams.
 */

#ifndef Stream_h
#define Stream_h

#include <inttypes.h>

#include "Print.h"

// Lookahead options used by parseInt()/parseFloat().
enum LookaheadMode {
    SKIP_ALL,
    SKIP_NONE,
    SKIP_WHITESPACE,
};

#define NO_IGNORE_CHAR '\x01'

class Stream : public Print {
protected:
    unsigned long _timeout;
    unsigned long _startMillis;

    int timedRead();
    int timedPeek();
    int peekNextDigit(LookaheadMode lookahead, bool detectDecimal);

public:
    Stream() : _timeout(1000UL), _startMillis(0UL) {}

    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;

    void setTimeout(unsigned long timeout);
    unsigned long getTimeout() const { return _timeout; }

    bool find(char* target);
    bool find(uint8_t* target) { return find(reinterpret_cast<char*>(target)); }
    bool find(char* target, size_t length);
    bool find(uint8_t* target, size_t length) {
        return find(reinterpret_cast<char*>(target), length);
    }
    bool find(char target) { return find(&target, 1U); }

    bool findUntil(char* target, char* terminator);
    bool findUntil(uint8_t* target, char* terminator) {
        return findUntil(reinterpret_cast<char*>(target), terminator);
    }
    bool findUntil(char* target, size_t targetLen, char* terminate, size_t termLen);
    bool findUntil(uint8_t* target, size_t targetLen, char* terminate, size_t termLen) {
        return findUntil(reinterpret_cast<char*>(target), targetLen, terminate, termLen);
    }

    long parseInt(LookaheadMode lookahead = SKIP_ALL, char ignore = NO_IGNORE_CHAR);
    float parseFloat(LookaheadMode lookahead = SKIP_ALL, char ignore = NO_IGNORE_CHAR);

    size_t readBytes(char* buffer, size_t length);
    size_t readBytes(uint8_t* buffer, size_t length) {
        return readBytes(reinterpret_cast<char*>(buffer), length);
    }
    size_t readBytesUntil(char terminator, char* buffer, size_t length);
    size_t readBytesUntil(char terminator, uint8_t* buffer, size_t length) {
        return readBytesUntil(terminator, reinterpret_cast<char*>(buffer), length);
    }

    String readString();
    String readStringUntil(char terminator);

protected:
    long parseInt(char ignore) { return parseInt(SKIP_ALL, ignore); }
    float parseFloat(char ignore) { return parseFloat(SKIP_ALL, ignore); }

    struct MultiTarget {
        const char* str;
        size_t len;
        size_t index;
    };

    int findMulti(struct MultiTarget* targets, int tCount);
};

#undef NO_IGNORE_CHAR

#endif
