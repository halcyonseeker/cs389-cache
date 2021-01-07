/**
 * cache_store.cc
 * Talib Pierson & Thalia Wright
 * September 2020
 * Implement the look-aside cache interface in cache.hh.
 */
#include <cassert>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <utility>

#include "cache.hh"
#include "fifo_evictor.hh"

/**
 * Implement the private parts of Cache using the pimpl idiom.
 * Elements of Impl need to be public, so a struct makes sense here
 */
class Cache::Impl {
public:
    // These are set in the constructor
    size_type maxmem;
    Evictor *evictor;  // A pointer to the evictor

    mutable size_t successful_gets = 0;  // Number of successful calls to get()
    mutable size_t gets = 0;             // Number of calls to get

    // The table is a std::unordered_map
    // std::unordered_map<key_type, val_type> table;

    // The following line may be uncommented to use a custom hasher.
    // We have not figure out how to get that working yet
    // default constructor: empty map
    std::unordered_map<key_type, val_type, hash_func> table;

    Impl(size_type max_mem, float max_load_factor, Evictor *p_evictor,
         hash_func hasher)
            : table(0, hasher) {
        this->maxmem = max_mem;
        table.max_load_factor(max_load_factor);
        this->evictor = p_evictor;  // Use the evictor
    }
};

/**
 * Create a new cache object with the following parameters.
 * @param maxmem            The maximum allowance for storage used by values.
 * @param max_load_factor   Maximum allowed ratio between buckets and table
 * rows.
 * @param evictor           Eviction policy implementation.
 *                          If nullptr, no evictions occur.
 *                          New insertions fail after maxmem has been exceeded.
 * @param hasher            Hash function to use on the keys.
 *                          Defaults to C++'s std::hash.
 */
Cache::Cache(size_type maxmem, float max_load_factor, Evictor *evictor,
             hash_func hasher)
        : pImpl_(new Impl(maxmem, max_load_factor, evictor, hasher)) {}

/**
 * Define a destructor to clean up the data buffers
 */
Cache::~Cache() {
    delete this->pImpl_->evictor;
    assert(Cache::reset());
}

/**
 * Add a <key, value> pair to the cache.
 * If key already exists, it will overwrite the old value.
 * Both the key and the value are to be deep-copied (not just pointer copied).
 * If maxmem capacity is exceeded, enough values will be removed
 * from the cache to accommodate the new value. If unable, the new value
 * isn't inserted to the cache.
 * @param key string
 * @param val struct
 * @return true iff the insertion of the data to the store was successful.
 */
bool Cache::set(key_type key, val_type val) {
    // copy key; '=' copies c++ 'std::string's
    key_type key_cpy = key;

    // Register key with the evictor
    if (this->pImpl_->evictor != nullptr) {
        this->pImpl_->evictor->touch_key(key);
    }

    // Find things to evict
    size_type memused = this->space_used() + val.size_;
    while (memused > this->pImpl_->maxmem) {
        if (this->pImpl_->evictor != nullptr) {
            // custom hasher should not cause SIGABRT
            try {
                key_type to_evict = this->pImpl_->evictor->evict();
                if (to_evict.empty()) return false;
                while (this->pImpl_->table.find(to_evict) ==
                       this->pImpl_->table.end()) {
                    to_evict = this->pImpl_->evictor->evict();
                    if (to_evict.empty()) return false;
                }
                memused -= this->pImpl_->table.at(to_evict).size_;
                this->del(to_evict);
            } catch (const std::exception &e) {
                std::cerr << "Cache::set(): evict, find, at, del: " << e.what()
                          << std::endl;
                return false;
            }
        } else {
            return false;
        }
    }

    try {
        // Check to see if 'key' already exists; overwrite it
        // Does this work when using a custom hasher?
        if (this->pImpl_->table.find(key) != this->pImpl_->table.end()) {
            this->del(key);
        }
    } catch (const std::exception &e) {
        std::cout << "Cache::set(): find, del: " << e.what() << std::endl;
    }

    // copy val; do we need to do this?
    auto *data_cpy = new byte_type[val.size_];
    memcpy(data_cpy, val.data_, val.size_);
    val_type val_cpy{data_cpy, val.size_};

    try {
        this->pImpl_->table.insert(std::make_pair(key_cpy, val_cpy));
    } catch (const std::exception &e) {  /// TODO: !untested?
        // we have to delete it if it wasn't inserted
        delete[] data_cpy;
        std::cerr << "Cache::set(): insert: " << e.what() << std::endl;
        return false;
    }

    return true;
}

/**
 * @param key string
 * @return val: a copy of the value associated with key in the cache,
 *         or nullptr with size 0 if not found.
 *         Note that the data_ pointer in the return key is a newly-allocated
 *         copy of the data. It is the caller's responsibility to free it.
 */
Cache::val_type Cache::get(key_type key) const {
    this->pImpl_->gets++;

    // return
    // val_type val = {.data_: nullptr, .size_: 0}
    Cache::val_type return_val{nullptr, 0};

    try {
        // return if the key doesn't exist
        if (this->pImpl_->table.find(key) == this->pImpl_->table.end()) {
            return return_val;
        }
    } catch (const std::exception &e) {
        std::cout << "Cache::get(): find: " << e.what() << std::endl;
        return return_val;
    }

    Cache::val_type found_val{nullptr, 0};
    try {
        // save a the value
        found_val = this->pImpl_->table.at(key);
    } catch (const std::exception &e) {
        std::cerr << "Cache::get(): at: " << e.what() << std::endl;
        return return_val;
    }

    // deep copy buff from found_val to return_val
    auto *buff = new byte_type[found_val.size_];
    memcpy(buff, found_val.data_, found_val.size_);
    return_val = {buff, found_val.size_};

    this->pImpl_->successful_gets++;

    return return_val;
}

/**
 *  Delete object from Cache if object in Cache.
 *  Erase pair at key in table; return true if key in table else false.
 *  @param key of pair to erase
 *  @return true if pair erased else false
 *
 *  !If key or value in pair is pointer, pointed-to memory not freed.
 *  User is responsible for calling reset to free pointed-to memory.
 */
bool Cache::del(key_type key) {
    try {
        // return if the key doesn't exist
        if (this->pImpl_->table.find(key) == this->pImpl_->table.end()) {
            return false;
        }
    } catch (const std::exception &e) {
        std::cout << "Cache::del: find: " << e.what() << std::endl;
        return false;
    }

    const byte_type *buff;
    try {
        // Save a pointer to the data buffer
        buff = this->pImpl_->table.at(key).data_;
    } catch (const std::exception &e) {
        std::cout << "Cache::del: at: " << e.what() << std::endl;
        return false;
    }

    bool ret;
    try {
        // Remove the entry
        ret = this->pImpl_->table.erase(key);
    } catch (const std::exception &e) {
        std::cout << "Cache::del: erase: " << e.what() << std::endl;
        return false;
    }

    // Free the buffer
    delete[] buff;

    return ret;
}

/**
 * @return the total amount of memory used up by all cache values (not keys).
 */
Cache::size_type Cache::space_used() const {
    size_type mem = 0;
    for (const auto &entry : this->pImpl_->table) {
        mem += entry.second.size_;
    }
    return mem;
}

/**
 * @return the ratio of gets that had been successful.
 */
double Cache::hit_rate() const {
    if (this->pImpl_->gets == 0) return 0;
    return static_cast<double>(this->pImpl_->successful_gets) /
           static_cast<double>(this->pImpl_->gets);
}

/**
 * Delete all data from the cache.
 * @return true iff successful.
 */
bool Cache::reset() {
    // Make sure the value data are all cleaned up
    for (auto pair = this->pImpl_->table.begin();
         pair != this->pImpl_->table.end();
         pair = this->pImpl_->table.erase(pair)) {
        delete[] pair->second.data_;
    }
    this->pImpl_->successful_gets = 0;  // Number of successful calls to get()
    this->pImpl_->gets = 0;             // Number of calls to get
    return this->pImpl_->table.empty();
}
