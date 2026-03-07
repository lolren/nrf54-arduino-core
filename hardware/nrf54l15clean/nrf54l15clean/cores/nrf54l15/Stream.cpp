/*
 * Arduino Stream parsing helpers.
 */

#include "Arduino.h"
#include "Stream.h"

#include <string.h>

int Stream::timedRead() {
    int c;
    _startMillis = millis();
    do {
        c = read();
        if (c >= 0) {
            return c;
        }
    } while (millis() - _startMillis < _timeout);
    return -1;
}

int Stream::timedPeek() {
    int c;
    _startMillis = millis();
    do {
        c = peek();
        if (c >= 0) {
            return c;
        }
    } while (millis() - _startMillis < _timeout);
    return -1;
}

int Stream::peekNextDigit(LookaheadMode lookahead, bool detectDecimal) {
    int c;

    while (true) {
        c = timedPeek();

        if (c < 0 || c == '-' || (c >= '0' && c <= '9') || (detectDecimal && c == '.')) {
            return c;
        }

        switch (lookahead) {
            case SKIP_NONE:
                return -1;
            case SKIP_WHITESPACE:
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                    return -1;
                }
                break;
            case SKIP_ALL:
            default:
                break;
        }

        (void)read();
    }
}

void Stream::setTimeout(unsigned long timeout) {
    _timeout = timeout;
}

bool Stream::find(char* target) {
    return findUntil(target, strlen(target), nullptr, 0U);
}

bool Stream::find(char* target, size_t length) {
    return findUntil(target, length, nullptr, 0U);
}

bool Stream::findUntil(char* target, char* terminator) {
    return findUntil(target, strlen(target), terminator, strlen(terminator));
}

bool Stream::findUntil(char* target, size_t targetLen, char* terminator, size_t termLen) {
    if (target == nullptr || targetLen == 0U) {
        return false;
    }

    if (terminator == nullptr || termLen == 0U) {
        MultiTarget targets[1] = {{target, targetLen, 0U}};
        return findMulti(targets, 1) == 0;
    }

    MultiTarget targets[2] = {{target, targetLen, 0U}, {terminator, termLen, 0U}};
    return findMulti(targets, 2) == 0;
}

long Stream::parseInt(LookaheadMode lookahead, char ignore) {
    bool isNegative = false;
    long value = 0;

    int c = peekNextDigit(lookahead, false);
    if (c < 0) {
        return 0;
    }

    do {
        if (c == ignore) {
            // ignored
        } else if (c == '-') {
            isNegative = true;
        } else if (c >= '0' && c <= '9') {
            value = value * 10 + c - '0';
        }

        (void)read();
        c = timedPeek();
    } while ((c >= '0' && c <= '9') || c == ignore);

    if (isNegative) {
        value = -value;
    }
    return value;
}

float Stream::parseFloat(LookaheadMode lookahead, char ignore) {
    bool isNegative = false;
    bool isFraction = false;
    long value = 0;
    float fraction = 1.0f;

    int c = peekNextDigit(lookahead, true);
    if (c < 0) {
        return 0.0f;
    }

    do {
        if (c == ignore) {
            // ignored
        } else if (c == '-') {
            isNegative = true;
        } else if (c == '.') {
            isFraction = true;
        } else if (c >= '0' && c <= '9') {
            value = value * 10 + c - '0';
            if (isFraction) {
                fraction *= 0.1f;
            }
        }

        (void)read();
        c = timedPeek();
    } while ((c >= '0' && c <= '9') || (c == '.' && !isFraction) || c == ignore);

    if (isNegative) {
        value = -value;
    }

    return isFraction ? static_cast<float>(value) * fraction : static_cast<float>(value);
}

size_t Stream::readBytes(char* buffer, size_t length) {
    if (buffer == nullptr || length == 0U) {
        return 0U;
    }

    size_t count = 0U;
    while (count < length) {
        const int c = timedRead();
        if (c < 0) {
            break;
        }
        *buffer++ = static_cast<char>(c);
        ++count;
    }

    return count;
}

size_t Stream::readBytesUntil(char terminator, char* buffer, size_t length) {
    if (buffer == nullptr || length == 0U) {
        return 0U;
    }

    size_t count = 0U;
    while (count < length) {
        const int c = timedRead();
        if (c < 0 || c == terminator) {
            break;
        }
        *buffer++ = static_cast<char>(c);
        ++count;
    }

    return count;
}

String Stream::readString() {
    String out;
    int c = timedRead();
    while (c >= 0) {
        out += static_cast<char>(c);
        c = timedRead();
    }
    return out;
}

String Stream::readStringUntil(char terminator) {
    String out;
    int c = timedRead();
    while (c >= 0 && c != terminator) {
        out += static_cast<char>(c);
        c = timedRead();
    }
    return out;
}

int Stream::findMulti(struct Stream::MultiTarget* targets, int tCount) {
    if (targets == nullptr || tCount <= 0) {
        return -1;
    }

    for (struct MultiTarget* t = targets; t < (targets + tCount); ++t) {
        if (t->len == 0U) {
            return static_cast<int>(t - targets);
        }
    }

    while (true) {
        const int c = timedRead();
        if (c < 0) {
            return -1;
        }

        for (struct MultiTarget* t = targets; t < (targets + tCount); ++t) {
            if (c == t->str[t->index]) {
                if (++t->index == t->len) {
                    return static_cast<int>(t - targets);
                }
                continue;
            }

            if (t->index == 0U) {
                continue;
            }

            const size_t originalIndex = t->index;
            do {
                --t->index;

                if (c != t->str[t->index]) {
                    continue;
                }

                if (t->index == 0U) {
                    ++t->index;
                    break;
                }

                const size_t diff = originalIndex - t->index;
                size_t i = 0U;
                for (; i < t->index; ++i) {
                    if (t->str[i] != t->str[i + diff]) {
                        break;
                    }
                }

                if (i == t->index) {
                    ++t->index;
                    break;
                }
            } while (t->index != 0U);
        }
    }
}
