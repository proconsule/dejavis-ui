#ifndef DEJAVIS_MULTICHANNEL_RINGBUFFER_H
#define DEJAVIS_MULTICHANNEL_RINGBUFFER_H

#include <vector>
#include <atomic>
#include <algorithm>
#include <cstring>

class MultiChannelRingBuffer {
private:
    std::vector<std::vector<float>> buffers;       // [canale][sample]
    alignas(64) std::atomic<size_t> writePtr{0};   // in FRAMES
    alignas(64) std::atomic<size_t> readPtr{0};    // in FRAMES
    size_t size;                                   // capacità per canale (frame, potenza di 2)
    size_t mask;
    int    channels;

public:
    MultiChannelRingBuffer(int num_channels = 2, size_t capacity_frames = 4096)
        : channels(num_channels)
    {
        size = 1;
        while (size < capacity_frames) size <<= 1;
        mask = size - 1;
        buffers.resize(channels);
        for (auto& b : buffers) b.resize(size);
    }

    bool write(const float* const* data, size_t frames) {
        size_t w = writePtr.load(std::memory_order_relaxed);
        size_t r = readPtr.load(std::memory_order_acquire);

        if (size - (w - r) < frames) return false;

        size_t offset = w & mask;
        size_t first  = std::min(frames, size - offset);  // fino al wrap
        size_t second = frames - first;

        for (int ch = 0; ch < channels; ch++) {
            std::memcpy(&buffers[ch][offset], data[ch], first * sizeof(float));
            if (second) {
                std::memcpy(&buffers[ch][0], data[ch] + first, second * sizeof(float));
            }
        }

        writePtr.store(w + frames, std::memory_order_release);
        return true;
    }

    bool read(float* const* data, size_t frames) {
        size_t w = writePtr.load(std::memory_order_acquire);
        size_t r = readPtr.load(std::memory_order_relaxed);

        if (w - r < frames) return false;

        size_t offset = r & mask;
        size_t first  = std::min(frames, size - offset);
        size_t second = frames - first;

        for (int ch = 0; ch < channels; ch++) {
            std::memcpy(data[ch], &buffers[ch][offset], first * sizeof(float));
            if (second) {
                std::memcpy(data[ch] + first, &buffers[ch][0], second * sizeof(float));
            }
        }

        readPtr.store(r + frames, std::memory_order_release);
        return true;
    }

    bool peek(float* const* data, size_t frames) const {
        size_t w = writePtr.load(std::memory_order_acquire);
        size_t r = readPtr.load(std::memory_order_relaxed);

        if (w - r < frames) return false;

        size_t offset = r & mask;
        size_t first  = std::min(frames, size - offset);
        size_t second = frames - first;

        for (int ch = 0; ch < channels; ch++) {
            std::memcpy(data[ch], &buffers[ch][offset], first * sizeof(float));
            if (second) {
                std::memcpy(data[ch] + first, &buffers[ch][0], second * sizeof(float));
            }
        }
        return true;
    }

    size_t getAvailableRead()  const { return writePtr.load() - readPtr.load(); }
    size_t getAvailableWrite() const { return size - (writePtr.load() - readPtr.load()); }
    size_t getCapacity()       const { return size; }
    int    getChannels()       const { return channels; }

    void reset() {
        writePtr.store(0);
        readPtr.store(0);
        for (auto& b : buffers) std::fill(b.begin(), b.end(), 0.0f);
    }

    void skip(size_t frames) {
        size_t w = writePtr.load(std::memory_order_acquire);
        size_t r = readPtr.load(std::memory_order_relaxed);
        size_t available = w - r;
        size_t toSkip = std::min(frames, available);
        readPtr.store(r + toSkip, std::memory_order_release);
    }

    size_t getWritePtr() const { return writePtr.load(std::memory_order_acquire); }
    size_t getReadPtr()  const { return readPtr.load(std::memory_order_acquire); }
};

#endif