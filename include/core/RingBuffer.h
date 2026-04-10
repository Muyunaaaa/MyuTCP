#pragma once
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace myu {
    template <typename T, size_t Capacity>
    class RingQueue {
        static_assert(Capacity > 0, "RingQueue Capacity must be > 0");

    public:
        RingQueue() = default;

        void push(const T& value) {
            buffer_[head_] = value;
            advance();
        }

        void push(T&& value) {
            buffer_[head_] = std::move(value);
            advance();
        }

        bool push_batch(std::span<const T> values) {
            if (values.size() > (Capacity - size_)) {
                return false; // Not enough space to push all values
            }

            for (const auto& value : values) {
                buffer_[head_] = value;
                head_ = (head_ + 1) % Capacity;
            }
            size_ += values.size();
            return true;
        }

        // when the buffer is full, try_push will return false, otherwise it will push the value and return true
        bool try_push(const T& value) {
            if (full()) return false;
            buffer_[head_] = value;
            head_ = (head_ + 1) % Capacity;
            ++size_;
            return true;
        }

        std::vector<T> peek_range(size_t offset, size_t n) const {
            if (offset >= size_ || n == 0) return {};

            size_t actual_n = ((offset + n) > size_) ? (size_ - offset) : n;

            std::vector<T> result;
            result.reserve(actual_n);

            for (size_t i = 0; i < actual_n; ++i) {
                result.push_back((*this)[offset + i]);
            }
            return result;
        }

        void pop_front(size_t n) {
            if (n == 0) return;

            size_t actual_to_pop = (n > size_) ? size_ : n;

            tail_ = (tail_ + actual_to_pop) % Capacity;
            size_ -= actual_to_pop;
        }

        bool pop(T& out) {
            if (size_ == 0) {
                return false;
            }

            out = std::move(buffer_[tail_]);
            tail_ = (tail_ + 1) % Capacity;
            --size_;
            return true;
        }

        T& front() {
            return buffer_[tail_];
        }

        const T& front() const {
            return buffer_[tail_];
        }

        T& back() {
            size_t idx = (head_ + Capacity - 1) % Capacity;
            return buffer_[idx];
        }

        const T& back() const {
            size_t idx = (head_ + Capacity - 1) % Capacity;
            return buffer_[idx];
        }

        bool empty() const {
            return size_ == 0;
        }

        bool full() const {
            return size_ == Capacity;
        }

        size_t size() const {
            return size_;
        }

        constexpr size_t capacity() const {
            return Capacity;
        }

        void clear() {
            head_ = 0;
            tail_ = 0;
            size_ = 0;
        }

        T& operator[](size_t logicalIndex) {
            size_t physicalIndex = (tail_ + logicalIndex) % Capacity;
            return buffer_[physicalIndex];
        }

        const T& operator[](size_t logicalIndex) const {
            size_t physicalIndex = (tail_ + logicalIndex) % Capacity;
            return buffer_[physicalIndex];
        }

    private:
        void advance() {
            head_ = (head_ + 1) % Capacity;

            if (size_ < Capacity) {
                ++size_;
            } else {
                tail_ = (tail_ + 1) % Capacity;
            }
        }

    private:
        std::array<T, Capacity> buffer_{};
        size_t head_ = 0;
        size_t tail_ = 0;
        size_t size_ = 0;
    };
}
