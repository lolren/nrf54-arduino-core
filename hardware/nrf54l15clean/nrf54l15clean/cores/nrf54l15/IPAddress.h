/*
 * Arduino IPAddress (IPv4) compatibility class.
 */

#ifndef IPAddress_h
#define IPAddress_h

#include <stdint.h>

#include "Printable.h"
#include "WString.h"

class IPAddress : public Printable {
private:
    union {
        uint8_t bytes[4];
        uint32_t dword;
    } _address;

    uint8_t* raw_address() { return _address.bytes; }

public:
    IPAddress();
    IPAddress(uint8_t firstOctet, uint8_t secondOctet, uint8_t thirdOctet, uint8_t fourthOctet);
    IPAddress(uint32_t address);
    IPAddress(const uint8_t* address);

    bool fromString(const char* address);
    bool fromString(const String& address) { return fromString(address.c_str()); }

    operator uint32_t() const { return _address.dword; }
    bool operator==(const IPAddress& addr) const { return _address.dword == addr._address.dword; }
    bool operator==(const uint8_t* addr) const;

    uint8_t operator[](int index) const { return _address.bytes[index]; }
    uint8_t& operator[](int index) { return _address.bytes[index]; }

    IPAddress& operator=(const uint8_t* address);
    IPAddress& operator=(uint32_t address);

    size_t printTo(Print& p) const override;

    friend class UDP;
    friend class Client;
    friend class Server;
};

extern const IPAddress INADDR_NONE;

#endif
