/**
 * fifo_evictor.cc
 * Talib Pierson & Thalia Wright
 * September 2020
 * Declare the FIFO eviction policy interface.
 */

#pragma once

#include <queue>

#include "evictor.hh"

class Fifo_Evictor : virtual public Evictor {
private:
    std::queue<key_type> keys;

public:
    Fifo_Evictor();

    ~Fifo_Evictor() override;

    void touch_key(const key_type &) override;

    const key_type evict() override;
};
