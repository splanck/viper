/* C++ new/delete operators for ViperOS */

#include "../include/c++/new"
#include "../include/stdlib.h"

/* nothrow instance */
const std::nothrow_t std::nothrow{};

/* new_handler storage */
static std::new_handler current_new_handler = nullptr;

std::new_handler std::get_new_handler() noexcept
{
    return current_new_handler;
}

std::new_handler std::set_new_handler(std::new_handler new_p) noexcept
{
    std::new_handler old = current_new_handler;
    current_new_handler = new_p;
    return old;
}

/* Regular new */
void* operator new(std::size_t size)
{
    if (size == 0)
        size = 1;

    void* ptr = malloc(size);
    while (!ptr)
    {
        std::new_handler handler = std::get_new_handler();
        if (handler)
        {
            handler();
            ptr = malloc(size);
        }
        else
        {
            /* In a full implementation, would throw std::bad_alloc */
            /* For freestanding, we abort */
            abort();
        }
    }
    return ptr;
}

void* operator new[](std::size_t size)
{
    return operator new(size);
}

/* Nothrow new */
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (size == 0)
        size = 1;
    return malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return operator new(size, std::nothrow);
}

/* Regular delete */
void operator delete(void* ptr) noexcept
{
    free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    free(ptr);
}

/* Sized delete (C++14) */
void operator delete(void* ptr, std::size_t) noexcept
{
    free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    free(ptr);
}

/* Nothrow delete */
void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    free(ptr);
}
