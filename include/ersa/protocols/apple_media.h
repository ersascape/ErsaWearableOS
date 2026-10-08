#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace ersa { namespace protocols {
/**
 * Decoded AMS media state independent from BLE transport and device hardware.
 * A protocol adapter parses notifications into this value, then the companion
 * source publishes a semantic media update. Keeping parsing here allows the
 * exact same byte handling to run in host tests and in the firmware.
 */
struct AmsMedia {
    /** Current title, truncated and null-terminated to fixed embedded storage. */
    char title[32]{};
    /** Current artist name, truncated and null-terminated. */
    char artist[32]{};
    /** Whether the player reports playback rather than paused/stopped state. */
    bool playing{false};
    /**
     * Apply one AMS Entity Update notification.
     * @param bytes Notification payload beginning with entity/attribute IDs.
     * @param length Number of valid bytes in `bytes`.
     * @return True only when a recognized supported field changed; false for
     *         incomplete, unsupported, or duplicate data.
     * Strings are copied into bounded local buffers because BLE callback data
     * is transient and must not be retained after the callback returns.
     */
    bool update(const uint8_t* bytes, size_t length) {
        if (length < 3) return false;
        const uint8_t entity = bytes[0], attribute = bytes[1];
        const uint8_t* text = bytes + 3;
        const size_t size = length - 3;
        if (entity == 2 && (attribute == 0 || attribute == 2)) {
            char* target = attribute == 0 ? artist : title;
            const size_t count = size < 31 ? size : 31;
            char next[32]{};
            memcpy(next, text, count);
            if (strcmp(target, next) == 0) return false;
            memcpy(target, next, sizeof(next));
            return true;
        }
        if (entity == 0 && attribute == 1 && size >= 2 && text[1] == ',' && text[0] >= '0' && text[0] <= '3') {
            const bool nextPlaying = text[0] != '0';
            if (playing == nextPlaying) return false;
            playing = nextPlaying;
            return true;
        }
        return false;
    }
};
} }
