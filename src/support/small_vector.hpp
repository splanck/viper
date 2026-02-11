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

namespace il::support
{

/// @brief A vector-like container with inline storage for small element counts.
///
/// SmallVector<T, N> stores up to N elements in inline storage (no heap allocation).
/// When more than N elements are needed, it switches to heap allocation.
///
/// @tparam T Element type (must be trivially copyable for optimal performance).
/// @tparam N Number of elements to store inline (default: 8).
template <typename T, size_t N = 8> class SmallVector
{
    static_assert(N > 0, "SmallVector inline capacity must be positive");

    // Private members declared first so they can be used in inline methods
    T inlineStorage_[N]{}; ///< Inline storage for small vectors.
    T *heap_{nullptr};     ///< Heap storage when size > N.
    size_t capacity_{0};   ///< Heap capacity (0 when using inline).
    size_t size_{0};       ///< Current element count.

    /// @brief Check whether the vector is currently using heap storage.
    /// @return true if elements reside on the heap, false if inline.
    [[nodiscard]] bool isHeap() const noexcept
    {
        return heap_ != nullptr;
    }

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
    /// @param init List of elements to copy into the vector.
    SmallVector(std::initializer_list<T> init)
    {
        reserve(init.size());
        std::copy(init.begin(), init.end(), data());
        size_ = init.size();
    }

    /// @brief Copy constructor.
    /// @param other Source vector to copy elements from.
    SmallVector(const SmallVector &other)
    {
        reserve(other.size_);
        std::copy(other.data(), other.data() + other.size_, data());
        size_ = other.size_;
    }

    /// @brief Move constructor.
    /// @details If @p other uses heap storage, the buffer is stolen in O(1).
    ///          If @p other uses inline storage, elements are copied element-wise.
    /// @param other Source vector to move from; left empty after the move.
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
    /// @param other Source vector to copy elements from.
    /// @return Reference to this vector.
    SmallVector &operator=(const SmallVector &other)
    {
        if (this != &other)
        {
            clear();
            reserve(other.size_);
            std::copy(other.data(), other.data() + other.size_, data());
            size_ = other.size_;
        }
        return *this;
    }

    /// @brief Move assignment.
    /// @details Frees any existing heap buffer, then steals or copies from @p other.
    /// @param other Source vector to move from; left empty after the move.
    /// @return Reference to this vector.
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

    /// @brief Reserve capacity for at least @p n elements.
    /// @details If @p n exceeds current capacity, allocates a new heap buffer
    ///          and copies existing elements. No-op if capacity is already sufficient.
    /// @param n Minimum number of elements the vector should be able to hold.
    void reserve(size_t n)
    {
        if (n <= capacity())
            return;

        // Need to grow to heap
        T *newBuf = new T[n];
        std::copy(data(), data() + size_, newBuf);

        if (isHeap())
            delete[] heap_;

        heap_ = newBuf;
        capacity_ = n;
    }

    /// @brief Add an element to the end.
    /// @param value Element to copy-append.
    void push_back(const T &value)
    {
        if (size_ >= capacity())
            reserve(capacity() == 0 ? N : capacity() * 2);
        data()[size_++] = value;
    }

    /// @brief Add an element to the end (move version).
    /// @param value Element to move-append.
    void push_back(T &&value)
    {
        if (size_ >= capacity())
            reserve(capacity() == 0 ? N : capacity() * 2);
        data()[size_++] = std::move(value);
    }

    /// @brief Construct an element in place at the end.
    /// @tparam Args Constructor argument types.
    /// @param args Arguments forwarded to the element constructor.
    /// @return Reference to the newly constructed element.
    template <typename... Args> reference emplace_back(Args &&...args)
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
    void clear() noexcept
    {
        size_ = 0;
    }

    /// @brief Resize to @p n elements.
    /// @details New elements beyond the current size are default-initialized.
    ///          If @p n is smaller than size(), excess elements are logically removed.
    /// @param n Desired element count.
    void resize(size_t n)
    {
        if (n > capacity())
            reserve(n);
        size_ = n;
    }

    /// @brief Resize to @p n elements, filling new slots with @p value.
    /// @param n Desired element count.
    /// @param value Value to assign to newly created elements.
    void resize(size_t n, const T &value)
    {
        if (n > capacity())
            reserve(n);
        for (size_t i = size_; i < n; ++i)
            data()[i] = value;
        size_ = n;
    }

    // Accessors

    /// @brief Return the number of elements in the vector.
    /// @return Current element count.
    [[nodiscard]] size_t size() const noexcept
    {
        return size_;
    }

    /// @brief Return the total capacity (inline or heap).
    /// @return Maximum number of elements storable without reallocation.
    [[nodiscard]] size_t capacity() const noexcept
    {
        return isHeap() ? capacity_ : N;
    }

    /// @brief Return true if the vector contains no elements.
    /// @return true when size() == 0.
    [[nodiscard]] bool empty() const noexcept
    {
        return size_ == 0;
    }

    /// @brief Return a pointer to the underlying element storage.
    /// @return Pointer to the first element (inline or heap buffer).
    [[nodiscard]] T *data() noexcept
    {
        return isHeap() ? heap_ : inlineStorage_;
    }

    /// @brief Return a const pointer to the underlying element storage.
    /// @return Const pointer to the first element (inline or heap buffer).
    [[nodiscard]] const T *data() const noexcept
    {
        return isHeap() ? heap_ : inlineStorage_;
    }

    /// @brief Access element by index (unchecked in release builds).
    /// @param i Zero-based index; must be less than size().
    /// @return Reference to the element at position @p i.
    [[nodiscard]] reference operator[](size_t i) noexcept
    {
        assert(i < size_);
        return data()[i];
    }

    /// @brief Access element by index (const, unchecked in release builds).
    /// @param i Zero-based index; must be less than size().
    /// @return Const reference to the element at position @p i.
    [[nodiscard]] const_reference operator[](size_t i) const noexcept
    {
        assert(i < size_);
        return data()[i];
    }

    /// @brief Access the first element.
    /// @return Reference to the front element; undefined if empty.
    [[nodiscard]] reference front() noexcept
    {
        assert(size_ > 0);
        return data()[0];
    }

    /// @brief Access the first element (const).
    /// @return Const reference to the front element; undefined if empty.
    [[nodiscard]] const_reference front() const noexcept
    {
        assert(size_ > 0);
        return data()[0];
    }

    /// @brief Access the last element.
    /// @return Reference to the back element; undefined if empty.
    [[nodiscard]] reference back() noexcept
    {
        assert(size_ > 0);
        return data()[size_ - 1];
    }

    /// @brief Access the last element (const).
    /// @return Const reference to the back element; undefined if empty.
    [[nodiscard]] const_reference back() const noexcept
    {
        assert(size_ > 0);
        return data()[size_ - 1];
    }

    // Iterators

    /// @brief Return an iterator to the first element.
    /// @return Iterator pointing to the beginning of the element range.
    [[nodiscard]] iterator begin() noexcept
    {
        return data();
    }

    /// @brief Return a const iterator to the first element.
    /// @return Const iterator pointing to the beginning of the element range.
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return data();
    }

    /// @brief Return a const iterator to the first element.
    /// @return Const iterator pointing to the beginning of the element range.
    [[nodiscard]] const_iterator cbegin() const noexcept
    {
        return data();
    }

    /// @brief Return an iterator past the last element.
    /// @return Iterator pointing one past the last element.
    [[nodiscard]] iterator end() noexcept
    {
        return data() + size_;
    }

    /// @brief Return a const iterator past the last element.
    /// @return Const iterator pointing one past the last element.
    [[nodiscard]] const_iterator end() const noexcept
    {
        return data() + size_;
    }

    /// @brief Return a const iterator past the last element.
    /// @return Const iterator pointing one past the last element.
    [[nodiscard]] const_iterator cend() const noexcept
    {
        return data() + size_;
    }

    /// @brief Implicit conversion to span for API compatibility.
    /// @return A read-only span covering all elements.
    [[nodiscard]] operator std::span<const T>() const noexcept
    {
        return {data(), size_};
    }

    /// @brief Explicit conversion to mutable span.
    /// @return A mutable span covering all elements.
    [[nodiscard]] std::span<T> span() noexcept
    {
        return {data(), size_};
    }

    /// @brief Explicit conversion to const span.
    /// @return A read-only span covering all elements.
    [[nodiscard]] std::span<const T> span() const noexcept
    {
        return {data(), size_};
    }
};

} // namespace il::support
