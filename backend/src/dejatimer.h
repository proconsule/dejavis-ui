#ifndef DEJAVIS_TIMER_H
#define DEJAVIS_TIMER_H

#include <chrono>
#include <atomic>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <timeapi.h>
    #pragma comment(lib, "winmm.lib")
#endif

class CDEJATIMER {
private:
    using Clock = std::chrono::steady_clock;

    std::chrono::time_point<Clock> start_time;
    std::atomic<std::chrono::time_point<Clock>> last_time;
    std::atomic<std::chrono::time_point<Clock>> vis_time;
    std::atomic<double> delta_time;

    std::atomic<double> current_fps{0.0};
    int64_t frame_count = 0;
    std::chrono::time_point<Clock> last_fps_update;

public:
    CDEJATIMER() {
        #ifdef _WIN32
        timeBeginPeriod(1);
        #endif
        auto now = Clock::now();
        start_time = now;
        last_time.store(now);
        vis_time.store(now);
        last_fps_update = now;
        delta_time.store(0.0);
    }

    ~CDEJATIMER() {
        #ifdef _WIN32
        timeEndPeriod(1);
        #endif
    }

    void update() {
        auto current_time = Clock::now();
        std::chrono::duration<double> elapsed = current_time - last_time.load();

        delta_time.store(elapsed.count());
        last_time.store(current_time);

        frame_count++;
        auto fps_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_fps_update).count();

        if (fps_elapsed_ms >= 1000) {
            current_fps.store((frame_count * 1000.0) / fps_elapsed_ms);
            frame_count = 0;
            last_fps_update = current_time;
        }
    }

    void reset_vis_time() {
        vis_time.store(Clock::now());
    }

    int64_t get_vis_time_us() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(last_time.load() - vis_time.load());
        return elapsed.count();
    }

    int64_t get_now_vis_us() const {
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - vis_time.load());
        return elapsed.count();
    }

    double get_fps() const { return current_fps.load(); }
    double get_delta_time() const { return delta_time.load(); }

    double get_elapsed_since_last_update() const {
        auto now = Clock::now();
        std::chrono::duration<double> elapsed = now - last_time.load();
        return elapsed.count();
    }

    double get_elapsed_since_last_update_ms() const {
        auto now = Clock::now();
        std::chrono::duration<double, std::milli> elapsed = now - last_time.load();
        return elapsed.count();
    }

    int64_t get_elapsed_since_last_update_us() const {
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_time.load());
        return elapsed.count();
    }

    double get_total_time() const {
        std::chrono::duration<double> elapsed = last_time.load() - start_time;
        return elapsed.count();
    }
};

#endif