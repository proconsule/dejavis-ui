#ifndef DEJAVISRINGBUFFER_H
#define DEJAVISRINGBUFFER_H

#include <vector>
#include <atomic>
#include <algorithm>

class RingBuffer {
private:
    std::vector<float> buffer;
    alignas(64) std::atomic<size_t> writePtr{0};
    alignas(64) std::atomic<size_t> readPtr{0};
    size_t size;
    size_t mask;

public:
    RingBuffer(size_t capacity = 4096) {
        size = 1;
        while (size < capacity) size <<= 1;
        mask = size - 1;
        buffer.resize(size);
    }

    bool write(const float* data, size_t count) {
        size_t currentWrite = writePtr.load(std::memory_order_relaxed);
        size_t currentRead = readPtr.load(std::memory_order_acquire);

        if (size - (currentWrite - currentRead) < count) {
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            buffer[(currentWrite + i) & mask] = data[i];
        }

        writePtr.store(currentWrite + count, std::memory_order_release);
        return true;
    }

    bool read(float* data, size_t count) {
        size_t currentWrite = writePtr.load(std::memory_order_acquire);
        size_t currentRead = readPtr.load(std::memory_order_relaxed);

        if (currentWrite - currentRead < count) {
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            data[i] = buffer[(currentRead + i) & mask];
        }

        readPtr.store(currentRead + count, std::memory_order_release);
        return true;
    }

    bool peek(float* data, size_t count) const {
        size_t currentWrite = writePtr.load(std::memory_order_acquire);
        size_t currentRead = readPtr.load(std::memory_order_relaxed);

        if (currentWrite - currentRead < count) {
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            data[i] = buffer[(currentRead + i) & mask];
        }

        return true;
    }

    size_t getAvailableRead() const {
        return writePtr.load() - readPtr.load();
    }
	
	size_t getCapacity() const {
		return size;
	}
	
	size_t getAvailableWrite() const {
        
        return size - (writePtr.load() - readPtr.load());
    }
	
	void reset() {
		writePtr.store(0);
		readPtr.store(0);
		std::fill(buffer.begin(), buffer.end(), 0.0f);
	}

    void skip(size_t count) {
        size_t currentWrite = writePtr.load(std::memory_order_acquire);
        size_t currentRead = readPtr.load(std::memory_order_relaxed);

        size_t available = currentWrite - currentRead;
        size_t toSkip = std::min(count, available);

        readPtr.store(currentRead + toSkip, std::memory_order_release);
    }

    size_t getWritePtr() const {
        return writePtr.load(std::memory_order_acquire);
    }
    size_t getReadPtr() const {
        return readPtr.load(std::memory_order_acquire);
    }

};

#endif