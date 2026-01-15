#pragma once

#include "../cap/table.hpp"
#include "../include/types.hpp"

/**
 * @file object.hpp
 * @brief Base class for reference-counted kernel objects.
 *
 * @details
 * Many kernel subsystems expose objects (channels, blobs, files, etc.) via the
 * capability system. Those objects often need shared ownership semantics:
 * multiple capabilities may refer to the same underlying object and the object
 * must remain alive until the last reference is released.
 *
 * The `kobj::Object` class provides:
 * - A simple intrusive reference count.
 * - A kind tag used for runtime type identification and safe downcasting.
 *
 * Objects derived from `kobj::Object` are intended to be allocated on the heap
 * (e.g. via `new`) and released via @ref kobj::release.
 */
namespace kobj
{

// Base class for all kernel objects
// Provides reference counting and type identification
/**
 * @brief Intrusive reference-counted base for kernel objects.
 *
 * @details
 * The reference count is stored in the object itself (intrusive). This keeps
 * object ownership management lightweight and avoids a separate control block.
 *
 * The `kind_` tag is used by the capability layer and by the `as<T>()` helper
 * to safely downcast without relying on RTTI.
 */
class Object
{
  public:
    /**
     * @brief Construct an object with a specific kind tag.
     *
     * @param kind Capability kind used to identify the object type.
     */
    Object(cap::Kind kind) : kind_(kind), ref_count_(1) {}

    virtual ~Object() = default;

    // Reference counting
    /**
     * @brief Increment the reference count.
     *
     * @details
     * Call this when creating a new reference to the object (e.g. duplicating
     * a capability).
     */
    void ref()
    {
        ++ref_count_;
    }

    /**
     * @brief Decrement the reference count.
     *
     * @details
     * When the count reaches zero, the caller is responsible for destroying the
     * object (typically via @ref kobj::release).
     *
     * @return `true` if the count reached zero and the object should be deleted.
     */
    bool unref()
    {
        if (--ref_count_ == 0)
        {
            return true; // Caller should delete
        }
        return false;
    }

    /** @brief Return the current reference count. */
    u32 ref_count() const
    {
        return ref_count_;
    }

    // Type identification
    /** @brief Return the capability kind tag for this object. */
    cap::Kind kind() const
    {
        return kind_;
    }

    // Type-safe cast
    /**
     * @brief Downcast to a derived object type if the kind matches.
     *
     * @tparam T Derived object type with a static `KIND` constant.
     * @return Pointer to `T` on success, or `nullptr` if kind does not match.
     */
    template <typename T> T *as()
    {
        if (kind_ == T::KIND)
        {
            return static_cast<T *>(this);
        }
        return nullptr;
    }

    /**
     * @brief Const downcast to a derived object type if the kind matches.
     *
     * @tparam T Derived object type with a static `KIND` constant.
     * @return Pointer to `const T` on success, or `nullptr` if kind does not match.
     */
    template <typename T> const T *as() const
    {
        if (kind_ == T::KIND)
        {
            return static_cast<const T *>(this);
        }
        return nullptr;
    }

  protected:
    cap::Kind kind_;
    u32 ref_count_;
};

// Helper to release an object (decrements ref, deletes if zero)
/**
 * @brief Release a reference to an object and delete it when the last reference is gone.
 *
 * @details
 * This is the canonical helper for dropping ownership of a `kobj::Object*`.
 * It decrements the reference count and performs `delete` when the count
 * reaches zero.
 *
 * @param obj Object pointer (may be `nullptr`).
 */
inline void release(Object *obj)
{
    if (obj && obj->unref())
    {
        delete obj;
    }
}

/**
 * @brief RAII smart pointer for kernel objects.
 *
 * @details
 * Ref<T> provides automatic reference counting for kobj::Object-derived types.
 * When a Ref is constructed from a raw pointer, it takes ownership (does NOT
 * call ref() since the object starts with refcount 1). When copied, it calls
 * ref() on the object. When destroyed, it calls release().
 *
 * Usage:
 * @code
 * Ref<Blob> blob = Blob::create(4096);  // Takes ownership
 * Ref<Blob> blob2 = blob;               // Increments refcount
 * // blob and blob2 both go out of scope, refcount drops to 0, object deleted
 * @endcode
 *
 * @tparam T Type derived from kobj::Object.
 */
template <typename T> class Ref
{
  public:
    /// Default constructor - creates a null reference
    Ref() : ptr_(nullptr) {}

    /// Construct from raw pointer, taking ownership (no ref increment)
    explicit Ref(T *ptr) : ptr_(ptr) {}

    /// Copy constructor - increments reference count
    Ref(const Ref &other) : ptr_(other.ptr_)
    {
        if (ptr_)
            ptr_->ref();
    }

    /// Move constructor - steals the reference
    Ref(Ref &&other) noexcept : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }

    /// Destructor - releases the reference
    ~Ref()
    {
        if (ptr_)
            release(ptr_);
    }

    /// Copy assignment
    Ref &operator=(const Ref &other)
    {
        if (this != &other)
        {
            if (ptr_)
                release(ptr_);
            ptr_ = other.ptr_;
            if (ptr_)
                ptr_->ref();
        }
        return *this;
    }

    /// Move assignment
    Ref &operator=(Ref &&other) noexcept
    {
        if (this != &other)
        {
            if (ptr_)
                release(ptr_);
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    /// Reset to a new pointer (releases old reference)
    void reset(T *ptr = nullptr)
    {
        if (ptr_)
            release(ptr_);
        ptr_ = ptr;
    }

    /// Release ownership and return raw pointer
    T *release_ptr()
    {
        T *ptr = ptr_;
        ptr_ = nullptr;
        return ptr;
    }

    /// Dereference operators
    T *operator->() const
    {
        return ptr_;
    }

    T &operator*() const
    {
        return *ptr_;
    }

    /// Get raw pointer
    T *get() const
    {
        return ptr_;
    }

    /// Boolean conversion
    explicit operator bool() const
    {
        return ptr_ != nullptr;
    }

    /// Comparison
    bool operator==(const Ref &other) const
    {
        return ptr_ == other.ptr_;
    }

    bool operator!=(const Ref &other) const
    {
        return ptr_ != other.ptr_;
    }

  private:
    T *ptr_;
};

/// Helper to create a Ref from a raw pointer
template <typename T> Ref<T> make_ref(T *ptr)
{
    return Ref<T>(ptr);
}

} // namespace kobj
