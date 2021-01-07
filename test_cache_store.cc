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

// Two of the parameters for Cache::Cache(), used in init_cache()
static const Cache::size_type maxmem = 256;
static const float maxload = 0.75;

// Used to compute the data stored in the cache
static const size_t min_data = 0;
static const size_t max_data = 256;
static const char * val_stub = "https://www.gutenberg.org/files";

// Declare these out here so we don't need to pass around references

// A little helper function to build a predictable data string
static std::string make_data(size_t i) {
    return val_stub + std::to_string(i) + "/" + std::to_string(i) + ".txt";
}

////////////////////////////////////////////////////////////////////////
// The following four functions act on a predictable chunk of data and
// are used to make sure the cache can set(), get(), and del() data.
////////////////////////////////////////////////////////////////////////

/**
 * Compute and set a chunk of data in the cache
 * @return true iff the data are successfully set
 */
static bool set_data(std::shared_ptr<Cache> &cache) {
    for (size_t i = min_data; i < max_data; i++) {
        try {
            const key_type key = std::to_string(i);
            const std::string data = make_data(i);
            Cache::val_type val{};

            auto *data_buf = new Cache::byte_type[data.size() + 1];
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
            return false;
        }
    }
    return true;
}

/**
 * Recompute the data in the cache and make sure they're present
 * @return true iff the data are correctly returned by Cache::get()
 */
static bool data_are_valid(std::shared_ptr<Cache> &cache) {
    for (size_t i = min_data; i < max_data; i++) {
        Cache::val_type val{};
        try {
            key_type key = std::to_string(i);
            val = cache->get(key);
            // Some data might have been evicted
            if (val.data_ != nullptr && val.size_ != 0) {
                if (val.data_ != make_data(i) ||
                    val.size_ != (make_data(i).size() + 1)) {
                    return false; // FAIL
                }
                delete[] val.data_;
            }
        } catch (const std::exception &e) {
            std::cerr << "get_data(): " << e.what() << std::endl;
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
static bool get_data(std::shared_ptr<Cache> &cache) {
    for (size_t i = min_data; i < max_data; i++) {
        Cache::val_type val{};
        try {
            key_type key = std::to_string(i);
            val = cache->get(key);
            if (val.data_ == nullptr || val.size_ == 0) return false;
        } catch (const std::exception &e) {
            std::cerr << "get_data(): " << e.what() << std::endl;
            delete[] val.data_;
            return false;
        }
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
static bool del_data(std::shared_ptr<Cache> &cache) {
    for (size_t i = min_data; i < max_data; i++) {
        try {
            key_type key = std::to_string(i);
            // Make sure the item wasn't evicted before returning false
            Cache::val_type val = cache->get(key);
            if (val.data_ != nullptr && val.size_ != 0) {
                delete[] val.data_;
                if (!cache->del(key)) return false;
            }
            cache->del(key);
        } catch (const std::exception &e) {
            std::cerr << "get_data(): " << e.what() << std::endl;
            return false;
        }
    }
    return true;
}

TEST_CASE("Reset an empty cache") {

    Fifo_Evictor *evictor;
    std::shared_ptr<Cache> cache;
    try {
        Cache::hash_func hasher = std::hash<key_type>();
        evictor = new Fifo_Evictor();
        cache = std::make_shared<Cache>(maxmem, maxload, evictor, hasher);
    } catch (const std::exception &e) {
        std::cerr << "Init Cache 1: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
    
    REQUIRE(cache->reset() == true);
}

TEST_CASE("Normal set()s, get()s, and del()s") {

    Fifo_Evictor *evictor;
    std::shared_ptr<Cache> cache;
    try {
        Cache::hash_func hasher = std::hash<key_type>();
        evictor = new Fifo_Evictor();
        cache = std::make_shared<Cache>(maxmem, maxload, evictor, hasher);
    } catch (const std::exception &e) {
        std::cerr << "Init Cache 1: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
    
    SECTION("The cache has been filled with data") {
        REQUIRE(set_data(cache) == true);
        REQUIRE(data_are_valid(cache) == true); // FAIL
        REQUIRE(del_data(cache) == true);
        REQUIRE(cache->space_used() <= maxmem);
        REQUIRE(cache->hit_rate() >= 0);
        REQUIRE(cache->reset() == true);
    }
}

TEST_CASE("Edge cases") {
    
    Fifo_Evictor *evictor;
    std::shared_ptr<Cache> cache;
    try {
        Cache::hash_func hasher = std::hash<key_type>();
        evictor = new Fifo_Evictor();
        cache = std::make_shared<Cache>(maxmem, maxload, evictor, hasher);
    } catch (const std::exception &e) {
        std::cerr << "Init Cache 1: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    SECTION("Set keys twice") {
        REQUIRE(set_data(cache) == true);
        REQUIRE(set_data(cache) == true);
        SECTION("Get keys that were set twice") {
            REQUIRE(data_are_valid(cache) == true);
        }
        SECTION("Delete keys that were set twice") {
            REQUIRE(del_data(cache) == true);
            REQUIRE(get_data(cache) == false);
        }
    }

    SECTION("Get deleted keys") {
        REQUIRE(set_data(cache) == true);
        REQUIRE(del_data(cache) == true);
        REQUIRE(get_data(cache) == false);
    }

    SECTION("Double delete keys") {
        REQUIRE(set_data(cache) == true);
        REQUIRE(del_data(cache) == true);
        REQUIRE(del_data(cache) == true);
    }

    SECTION("Get keys that were never set") {
        REQUIRE(get_data(cache) == false);
    }

    SECTION("Delete keys that were never set") {
        REQUIRE(del_data(cache) == true);
    }

    REQUIRE(cache->space_used() <= maxmem);
    REQUIRE(cache->hit_rate() >= 0);
    REQUIRE(cache->reset() == true);
}
