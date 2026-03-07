/*
 * Arduino IPAddress (IPv4) compatibility class.
 */

#include "Arduino.h"
#include "IPAddress.h"

#include <string.h>

IPAddress::IPAddress() {
    _address.dword = 0U;
}

IPAddress::IPAddress(uint8_t firstOctet, uint8_t secondOctet, uint8_t thirdOctet,
                     uint8_t fourthOctet) {
    _address.bytes[0] = firstOctet;
    _address.bytes[1] = secondOctet;
    _address.bytes[2] = thirdOctet;
    _address.bytes[3] = fourthOctet;
}

IPAddress::IPAddress(uint32_t address) {
    _address.dword = address;
}

IPAddress::IPAddress(const uint8_t* address) {
    memcpy(_address.bytes, address, sizeof(_address.bytes));
}

bool IPAddress::fromString(const char* address) {
    if (address == nullptr) {
        return false;
    }

    uint16_t acc = 0U;
    uint8_t dots = 0U;

    while (*address != '\0') {
        const char c = *address++;

        if (c >= '0' && c <= '9') {
            acc = static_cast<uint16_t>(acc * 10U + static_cast<uint16_t>(c - '0'));
            if (acc > 255U) {
                return false;
            }
        } else if (c == '.') {
            if (dots == 3U) {
                return false;
            }
            _address.bytes[dots++] = static_cast<uint8_t>(acc);
            acc = 0U;
        } else {
            return false;
        }
    }

    if (dots != 3U) {
        return false;
    }

    _address.bytes[3] = static_cast<uint8_t>(acc);
    return true;
}

IPAddress& IPAddress::operator=(const uint8_t* address) {
    memcpy(_address.bytes, address, sizeof(_address.bytes));
    return *this;
}

IPAddress& IPAddress::operator=(uint32_t address) {
    _address.dword = address;
    return *this;
}

bool IPAddress::operator==(const uint8_t* addr) const {
    return memcmp(addr, _address.bytes, sizeof(_address.bytes)) == 0;
}

size_t IPAddress::printTo(Print& p) const {
    size_t count = 0U;
    for (int i = 0; i < 3; ++i) {
        count += p.print(_address.bytes[i], DEC);
        count += p.print('.');
    }
    count += p.print(_address.bytes[3], DEC);
    return count;
}

const IPAddress INADDR_NONE(0, 0, 0, 0);
