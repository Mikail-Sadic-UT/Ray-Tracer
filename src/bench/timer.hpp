/// @file timer.hpp
/// @brief Host wall-clock timers that report milliseconds (INTERFACES section 8).
#pragma once

#include <chrono>

namespace rt {

/// Stopwatch on `std::chrono::steady_clock`. Starts when constructed.
///
/// It only reads the clock. A caller that times asynchronous device work must synchronize the
/// device before calling elapsed_ms() (INTERFACES section 8: `cudaDeviceSynchronize` at stage
/// boundaries).
class WallTimer {
   public:
    /// Starts the timer.
    WallTimer() : begin_(std::chrono::steady_clock::now()) {}

    /// Restarts the timer from now.
    void restart() { begin_ = std::chrono::steady_clock::now(); }

    /// Milliseconds since construction or the last restart().
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin_)
            .count();
    }

   private:
    std::chrono::steady_clock::time_point begin_;
};

/// Measures the lifetime of a scope and stores it in a Timings field.
///
/// Assigns the elapsed milliseconds to @p slot when destroyed, replacing its previous value, as
/// `build_bvh` does for its components.
class ScopedTimer {
   public:
    /// Starts timing; the result goes to @p slot, which must outlive this object.
    explicit ScopedTimer(double& slot_ms) : slot_(slot_ms) {}

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    /// Stores the elapsed time.
    ~ScopedTimer() { slot_ = timer_.elapsed_ms(); }

   private:
    double& slot_;
    WallTimer timer_;
};

}  // namespace rt
