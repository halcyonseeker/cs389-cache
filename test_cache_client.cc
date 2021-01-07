/**
 * new_test.cc
 * Talib Pierson & Thalia Wright
 * September 2020
 * Test the cache but with catch.hpp
 */

#include <cstring>
#include <iostream>
#include <string>

#define CATCH_CONFIG_MAIN 
#include <catch2/catch.hpp>

#include "cache.hh"
#include "fifo_evictor.hh"

// Should be equivalent to values used by server in testing
static const Cache::size_type maxmem = 65536;

// Used to compute the data stored in the cache
static const size_t min_data = 1;
static const size_t max_data = 9;
static const char *val_stub = "https:%20%20www.gutenberg.org%20files";

// Declare these out here so we don't need to pass around references
static std::shared_ptr<Cache> cache;

// A little helper function to build a predictable data string
static std::string make_data(size_t i) {
    return val_stub + std::to_string(i) + "%20" + std::to_string(i) + ".txt";
}

////////////////////////////////////////////////////////////////////////
// The following four functions act on a predictable chunk of data and
// are used to make sure the cache can set(), get(), and del() data.
////////////////////////////////////////////////////////////////////////

/**
 * Compute and set a chunk of data in the cache
 * @return true iff the data are successfully set
 */
static bool set_data() {
    for (size_t i = min_data; i < max_data; i++) {
        Cache::byte_type *data_buf;
        try {
            const key_type key = std::to_string(i);
            const std::string data = make_data(i);
            Cache::val_type val{};

            data_buf = new Cache::byte_type[data.size() + 1];
            strncpy(data_buf, data.c_str(), data.length() + 1);
            val.size_ = static_cast<Cache::size_type>(data.size() + 1);
            val.data_ = data_buf;

            if (!cache->set(key, val)) {
                delete[] data_buf;
                return false;
            }
            delete[] data_buf;
        } catch (const std::exception &e) {
            std::cerr << "set_data(): " << e.what() << std::endl;
            delete[] data_buf;
            return false;
        }
    }
    return true;
}

/**
 * Recompute the data in the cache and make sure they're present
 * @return true iff the data are correctly returned by Cache::get()
 */
static bool data_are_valid() {
    for (size_t i = min_data; i < max_data; i++) {
        Cache::val_type val{};
        try {
            key_type key = std::to_string(i);
            val = cache->get(key);
            // Some data might have been evicted
            if (val.data_ != nullptr && val.size_ != 0) {
                if (val.data_ != make_data(i) ||
                    val.size_ != (make_data(i).size() + 1)) {
                    delete[] val.data_;
                    return false;
                }
                delete[] val.data_;
            }
            delete[] val.data_;
        } catch (const std::exception &e) {
            std::cerr << "data_are_valid(): " << e.what() << std::endl;
            delete[] val.data_;
            return false;
        }
    }
    return true;
}

/**
 * Get data from the cache without validating them.
 * @return true iff the Cache::get() worked
 */
static bool get_data() {
    for (size_t i = min_data; i < max_data; i++) {
        Cache::val_type val{};
        key_type key = std::to_string(i);
        try {
            val = cache->get(key);
        } catch (const std::exception &e) {
            std::cerr << "get_data(): " << e.what() << std::endl;
        }
        if (val.data_ == nullptr) return false;
        delete[] val.data_;
    }
    return true;
}

/**
 * Delete a bunch of data from the cache.
 * @return true if the data were successfully deleted or aren't present.
 * Basically, in order to account for eviction, it will return true
 * almost regardless of what Cache::del() returns.
 */
static bool del_data() {
    for (size_t i = min_data; i < max_data; i++) {
        key_type key = std::to_string(i);
        Cache::val_type val = cache->get(key);
        try {
            // Make sure the item wasn't evicted before returning false
            if (val.data_ != nullptr && val.size_ != 0) {
                delete[] val.data_;
                if (!cache->del(key)) return false;
            }
            delete[] val.data_;
            cache->del(key);
        } catch (const std::exception &e) {
            delete[] val.data_;
            std::cerr << "get_data(): " << e.what() << std::endl;
            return false;
        }
    }
    return true;
}

// We need to change this to work with the network
TEST_CASE("Initialize the cache") {
    try {
        Cache::hash_func hasher = std::hash<key_type>();
        cache = std::make_shared<Cache>("localhost", "42069");
    } catch (const std::exception &e) {
        std::cerr << "Init cache failed: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
}

TEST_CASE("Reset an empty cache") { REQUIRE(cache->reset() == true); }

TEST_CASE("Normal set()s, get()s, and del()s") {
    SECTION("The cache has been filled with data") {
        REQUIRE(set_data() == true);
        REQUIRE(data_are_valid() == true);
        REQUIRE(del_data() == true);
        REQUIRE(cache->space_used() <= maxmem);
        REQUIRE(cache->hit_rate() >= 0);
        REQUIRE(cache->reset() == true);
        REQUIRE(cache->reset() == true);
    }
}

TEST_CASE("Edge cases") {
    SECTION("Set keys twice") {
        REQUIRE(set_data() == true);
        REQUIRE(set_data() == true);

        SECTION("Get keys that were set twice") {
            REQUIRE(data_are_valid() == true);
        }

        SECTION("Delete keys that were set twice") {
            REQUIRE(del_data() == true);
            REQUIRE(get_data() == false);
        }
        REQUIRE(cache->reset() == true);
    }

    SECTION("Get deleted keys") {
        REQUIRE(set_data() == true);
        REQUIRE(del_data() == true);
        REQUIRE(get_data() == false);
        REQUIRE(cache->reset() == true);
    }

    SECTION("Double delete keys") {
        REQUIRE(set_data() == true);
        REQUIRE(del_data() == true);
        REQUIRE(del_data() == true);
        REQUIRE(cache->reset() == true);
    }

    SECTION("Get keys that were never set") {
        REQUIRE(get_data() == false);
        REQUIRE(cache->reset() == true);
    }

    SECTION("Delete keys that were never set") {
        REQUIRE(del_data() == true);
        REQUIRE(cache->reset() == true);
    }

    SECTION("Get statistics") {
        REQUIRE(set_data() == true);
        REQUIRE(data_are_valid() == true);
        REQUIRE(cache->space_used() <= maxmem);
        REQUIRE(cache->hit_rate() >= 0);
        REQUIRE(cache->reset() == true);
    }

    SECTION("Nonzero Hit-rate") {
        REQUIRE(set_data() == true);
        REQUIRE(data_are_valid() == true);
        REQUIRE(cache->hit_rate() > 0);
        REQUIRE(cache->reset() == true);
    }

    SECTION("Zero Hit-rate") {
        REQUIRE(cache->hit_rate() == 0);
        REQUIRE(cache->reset() == true);
    }
}
