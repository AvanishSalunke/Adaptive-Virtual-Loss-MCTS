// rng.hpp - a small, fast pseudo-random number generator.
//
// Every thread must own its own Rng (never share one): a shared generator
// would be a data race and would also serialise the threads. Seeding each
// thread with a different seed gives each an independent-looking stream.
#pragma once
#include <cstdint>

// SplitMix64: turns any 64-bit number into a well-mixed one. Used to
// scramble seeds, so that seeds 1, 2, 3, ... give unrelated generators.
inline uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

class Rng {
public:
    explicit Rng(uint64_t seed = 1) {
        uint64_t s = seed;
        state_ = splitmix64(s);
        // xorshift gets stuck forever if its state is 0, so never allow that.
        if (state_ == 0) state_ = 0x9E3779B97F4A7C15ULL;
    }

    // xorshift64*: three shift-xor steps, then a multiply to improve the bits.
    uint64_t next() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }

    // Uniform integer in [0, n). Uses the top 32 bits and a multiply-shift
    // instead of '%' (faster; the bias is about n / 2^32, negligible for small n).
    uint32_t next_below(uint32_t n) {
        return static_cast<uint32_t>(((next() >> 32) * n) >> 32);
    }

private:
    uint64_t state_;
};
