// Header include.
#include "Helper.h"

// Local includes.
#include "Constants.h"

// Library includes.
#include <string.h>

size_t Helper::getOccurences(uint8_t const * bytes, char symbol, unsigned int length) {
    size_t count = 0;
    if (bytes == nullptr) {
        return count;
    }
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] == symbol) {
            count++;
        }
    }
    return count;
}

bool Helper::stringIsNullorEmpty(char const * str) {
    return str == nullptr || str[0] == '\0';
}

// NIMA CHANGES - strip the + at the end of the base topic if it exists
size_t Helper::parseRequestId(const char* base_topic, const char* received_topic) {
    size_t base_len = strlen(base_topic);

    // If base_topic ends with '+' (wildcard), remove it for comparison
    if (base_len > 0 && base_topic[base_len - 1] == '+') {
        base_len--;  // exclude '+'
        // Also strip the '/' before it if present
        if (base_len > 0 && base_topic[base_len - 1] == '/') {
            base_len--;
        }
    }

    // Safety check: ensure received_topic is at least as long as base_len
    if (strlen(received_topic) <= base_len) {
        return 0; // invalid input
    }

    // Extract the numeric part
    return atoi(received_topic + base_len + 1); // skip the '/' before the number
}
//
// size_t Helper::parseRequestId(char const * base_topic, char const * received_topic) {
//     // Remove the not needed part of the received topic string, which is everything before the request id,
//     // therefore we ignore the section before that which is the base topic, that seperates the topic from the request id.
//     // Meaning the index we attempt to parse at, is simply the length of the base topic
//     return atoi(received_topic + strlen(base_topic));
// }
