//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/small_vector.hpp
// Purpose: Stack-optimized vector that avoids heap allocation for small sizes.
//
// SmallVector stores up to N elements inline (on the stack) and only allocates
// from the heap when the size exceeds N. This is particularly useful for
// function call arguments where most calls have few arguments.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <utility>

namespace viper::support
{

/// @brief A vector-like container with inline storage for small element counts.
///
/// SmallVector<T, N> stores up to N elements in inline storage (no heap allocation).
/// When more than N elements are needed, it switches to heap allocation.
///
/// @tparam T Element type (must be trivially copyable for optimal performance).
/// @tparam N Number of elements to store inline (default: 8).
template <typename T, size_t N = 8>
class SmallVector
{
    static_assert(N > 0, "SmallVector inline capacity must be positive");

    // Private members declared first so they can be used in inline methods
    T inlineStorage_[N]{};  ///< Inline storage for small vectors.
    T *heap_{nullptr};      ///< Heap storage when size > N.
    size_t capacity_{0};    ///< Heap capacity (0 when using inline).
    size_t size_{0};        ///< Current element count.

    [[nodiscard]] bool isHeap() const noexcept { return heap_ != nullptr; }

  public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = T &;
    using const_reference = const T &;
    using pointer = T *;
    using const_pointer = const T *;
    using iterator = T *;
    using const_iterator = const T *;

    /// @brief Construct an empty SmallVector.
    SmallVector() noexcept = default;

    /// @brief Construct from initializer list.
    SmallVector(std::initializer_list<T> init)
    {
        reserve(init.size());
        for (const auto &elem : init)
            push_back(elem);
    }

    /// @brief Copy constructor.
    SmallVector(const SmallVector &other)
    {
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i)
            push_back(other[i]);
    }

    /// @brief Move constructor.
    SmallVector(SmallVector &&other) noexcept
    {
        if (other.isHeap())
        {
            // Steal the heap buffer
            heap_ = other.heap_;
            capacity_ = other.capacity_;
            size_ = other.size_;
            other.heap_ = nullptr;
            other.capacity_ = 0;
            other.size_ = 0;
        }
        else
        {
            // Copy from inline storage
            for (size_t i = 0; i < other.size_; ++i)
                inlineStorage_[i] = other.inlineStorage_[i];
            size_ = other.size_;
            other.size_ = 0;
        }
    }

    /// @brief Destructor.
    ~SmallVector()
    {
        if (isHeap())
            delete[] heap_;
    }

    /// @brief Copy assignment.
    SmallVector &operator=(const SmallVector &other)
    {
        if (this != &other)
        {
            clear();
            reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i)
                push_back(other[i]);
        }
        return *this;
    }

    /// @brief Move assignment.
    SmallVector &operator=(SmallVector &&other) noexcept
    {
        if (this != &other)
        {
            if (isHeap())
                delete[] heap_;

            if (other.isHeap())
            {
                heap_ = other.heap_;
                capacity_ = other.capacity_;
                size_ = other.size_;
                other.heap_ = nullptr;
                other.capacity_ = 0;
                other.size_ = 0;
            }
            else
            {
                heap_ = nullptr;
                capacity_ = 0;
                for (size_t i = 0; i < other.size_; ++i)
                    inlineStorage_[i] = other.inlineStorage_[i];
                size_ = other.size_;
                other.size_ = 0;
            }
        }
        return *this;
    }

    /// @brief Reserve capacity for at least n elements.
    void reserve(size_t n)
    {
        if (n <= capacity())
            return;

        // Need to grow to heap
        T *newBuf = new T[n];
        T *src = data();
        for (size_t i = 0; i < size_; ++i)
            newBuf[i] = src[i];

        if (isHeap())
            delete[] heap_;

        heap_ = newBuf;
        capacity_ = n;
    }

    /// @brief Add an element to the end.
    void push_back(const T &value)
    {
        if (size_ >= capacity())
            reserve(capacity() == 0 ? N : capacity() * 2);
        data()[size_++] = value;
    }

    /// @brief Add an element to the end (move version).
    void push_back(T &&value)
    {
        if (size_ >= capacity())
            reserve(capacity() == 0 ? N : capacity() * 2);
        data()[size_++] = std::move(value);
    }

    /// @brief Construct an element in place at the end.
    template <typename... Args>
    reference emplace_back(Args &&...args)
    {
        if (size_ >= capacity())
            reserve(capacity() == 0 ? N : capacity() * 2);
        T *ptr = data() + size_++;
        *ptr = T(std::forward<Args>(args)...);
        return *ptr;
    }

    /// @brief Remove the last element.
    void pop_back()
    {
        assert(size_ > 0);
        --size_;
    }

    /// @brief Clear all elements.
    void clear() noexcept { size_ = 0; }

    /// @brief Resize to n elements.
    void resize(size_t n)
    {
        if (n > capacity())
            reserve(n);
        size_ = n;
    }

    /// @brief Resize to n elements with default value.
    void resize(size_t n, const T &value)
    {
        if (n > capacity())
            reserve(n);
        for (size_t i = size_; i < n; ++i)
            data()[i] = value;
        size_ = n;
    }

    // Accessors

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t capacity() const noexcept { return isHeap() ? capacity_ : N; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] T *data() noexcept { return isHeap() ? heap_ : inlineStorage_; }
    [[nodiscard]] const T *data() const noexcept { return isHeap() ? heap_ : inlineStorage_; }

    [[nodiscard]] reference operator[](size_t i) noexcept
    {
        assert(i < size_);
        return data()[i];
    }
    [[nodiscard]] const_reference operator[](size_t i) const noexcept
    {
        assert(i < size_);
        return data()[i];
    }

    [[nodiscard]] reference front() noexcept
    {
        assert(size_ > 0);
        return data()[0];
    }
    [[nodiscard]] const_reference front() const noexcept
    {
        assert(size_ > 0);
        return data()[0];
    }

    [[nodiscard]] reference back() noexcept
    {
        assert(size_ > 0);
        return data()[size_ - 1];
    }
    [[nodiscard]] const_reference back() const noexcept
    {
        assert(size_ > 0);
        return data()[size_ - 1];
    }

    // Iterators

    [[nodiscard]] iterator begin() noexcept { return data(); }
    [[nodiscard]] const_iterator begin() const noexcept { return data(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data(); }

    [[nodiscard]] iterator end() noexcept { return data() + size_; }
    [[nodiscard]] const_iterator end() const noexcept { return data() + size_; }
    [[nodiscard]] const_iterator cend() const noexcept { return data() + size_; }

    /// @brief Implicit conversion to span for API compatibility.
    [[nodiscard]] operator std::span<const T>() const noexcept { return {data(), size_}; }

    /// @brief Explicit conversion to span.
    [[nodiscard]] std::span<T> span() noexcept { return {data(), size_}; }
    [[nodiscard]] std::span<const T> span() const noexcept { return {data(), size_}; }
};

} // namespace viper::support
