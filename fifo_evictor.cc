/**
 * fifo_evictor.cc
 * Talib Pierson & Thalia Wright
 * October 2020
 * Implement the FIFO eviction policy interface in fifo_evictor.hh.
 */
#include "fifo_evictor.hh"

/**
 * A trivial constructor for a fifo evictor object.
 */
Fifo_Evictor::Fifo_Evictor() = default;

/**
 * A trivial destructor for a fifo evictor object.
 */
Fifo_Evictor::~Fifo_Evictor() = default;

/**
 * Let the evictor know about a new entry in the cache
 * @param key The key being added to/removed from/read from the cache
 */
void Fifo_Evictor::touch_key(const key_type &key) { this->keys.push(key); }

/**
 * Evict the oldest required members of the cache to make way for a new member
 * @return The key of the item to remove
 */
const key_type Fifo_Evictor::evict() {
    if (this->keys.empty()) {
        return "";
    }
    key_type key = this->keys.front();
    this->keys.pop();
    return key;
}
