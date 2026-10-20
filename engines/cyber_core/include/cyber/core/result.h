#pragma once

#include <cyber/core/assert.h>
#include <cyber/core/types.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace cyber::core
{

// A recoverable result. Exactly one of T or E is engaged for the lifetime of the
// object. Runtime code has no exceptions, so T and E must be nothrow move
// constructible. Copy operations are available only when both alternatives are
// nothrow copy constructible.
template <typename T, typename E>
class [[nodiscard]] Result
{
    static_assert(!std::is_reference_v<T>, "Result cannot hold a reference value");
    static_assert(!std::is_reference_v<E>, "Result cannot hold a reference error");
    static_assert(!std::is_void_v<T>, "Use Result<void, E> for an error-only result");
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "Result value must be nothrow move constructible");
    static_assert(std::is_nothrow_move_constructible_v<E>,
                  "Result error must be nothrow move constructible");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "Result value must have a non-throwing destructor");
    static_assert(std::is_nothrow_destructible_v<E>,
                  "Result error must have a non-throwing destructor");

public:
    using ValueType = T;
    using ErrorType = E;

    template <typename... Args>
        requires std::is_nothrow_constructible_v<T, Args...>
    [[nodiscard]] static Result Ok(Args&&... args) noexcept
    {
        return Result(OkTag{}, std::forward<Args>(args)...);
    }

    template <typename... Args>
        requires std::is_nothrow_constructible_v<E, Args...>
    [[nodiscard]] static Result Err(Args&&... args) noexcept
    {
        return Result(ErrTag{}, std::forward<Args>(args)...);
    }

    ~Result() noexcept
    {
        Destroy();
    }

    Result(Result&& other) noexcept : m_isOk(other.m_isOk)
    {
        if (m_isOk)
        {
            std::construct_at(&m_storage.value, std::move(other.m_storage.value));
        }
        else
        {
            std::construct_at(&m_storage.error, std::move(other.m_storage.error));
        }
    }

    Result& operator=(Result&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_isOk = other.m_isOk;
            if (m_isOk)
            {
                std::construct_at(&m_storage.value, std::move(other.m_storage.value));
            }
            else
            {
                std::construct_at(&m_storage.error, std::move(other.m_storage.error));
            }
        }
        return *this;
    }

    Result(const Result& other) noexcept
        requires(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_constructible_v<E>)
        : m_isOk(other.m_isOk)
    {
        if (m_isOk)
        {
            std::construct_at(&m_storage.value, other.m_storage.value);
        }
        else
        {
            std::construct_at(&m_storage.error, other.m_storage.error);
        }
    }

    Result& operator=(const Result& other) noexcept
        requires(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_constructible_v<E>)
    {
        if (this != &other)
        {
            Destroy();
            m_isOk = other.m_isOk;
            if (m_isOk)
            {
                std::construct_at(&m_storage.value, other.m_storage.value);
            }
            else
            {
                std::construct_at(&m_storage.error, other.m_storage.error);
            }
        }
        return *this;
    }

    [[nodiscard]] bool IsOk() const noexcept
    {
        return m_isOk;
    }

    [[nodiscard]] bool IsError() const noexcept
    {
        return !m_isOk;
    }

    explicit operator bool() const noexcept
    {
        return m_isOk;
    }

    [[nodiscard]] T& Value() & noexcept
    {
        CG_ASSERT(m_isOk);
        return m_storage.value;
    }

    [[nodiscard]] const T& Value() const& noexcept
    {
        CG_ASSERT(m_isOk);
        return m_storage.value;
    }

    [[nodiscard]] T&& Value() && noexcept
    {
        CG_ASSERT(m_isOk);
        return std::move(m_storage.value);
    }

    [[nodiscard]] E& Error() & noexcept
    {
        CG_ASSERT(!m_isOk);
        return m_storage.error;
    }

    [[nodiscard]] const E& Error() const& noexcept
    {
        CG_ASSERT(!m_isOk);
        return m_storage.error;
    }

    [[nodiscard]] E&& Error() && noexcept
    {
        CG_ASSERT(!m_isOk);
        return std::move(m_storage.error);
    }

private:
    struct OkTag
    {
    };

    struct ErrTag
    {
    };

    union Storage
    {
        Storage() noexcept {}

        ~Storage() noexcept {}

        T value;
        E error;
    };

    template <typename... Args>
    explicit Result(OkTag, Args&&... args) noexcept : m_isOk(true)
    {
        std::construct_at(&m_storage.value, std::forward<Args>(args)...);
    }

    template <typename... Args>
    explicit Result(ErrTag, Args&&... args) noexcept : m_isOk(false)
    {
        std::construct_at(&m_storage.error, std::forward<Args>(args)...);
    }

    void Destroy() noexcept
    {
        if (m_isOk)
        {
            std::destroy_at(&m_storage.value);
        }
        else
        {
            std::destroy_at(&m_storage.error);
        }
    }

    Storage m_storage;
    bool m_isOk;
};

template <typename E>
class [[nodiscard]] Result<void, E>
{
    static_assert(!std::is_reference_v<E>, "Result cannot hold a reference error");
    static_assert(std::is_nothrow_move_constructible_v<E>,
                  "Result error must be nothrow move constructible");
    static_assert(std::is_nothrow_destructible_v<E>,
                  "Result error must have a non-throwing destructor");

public:
    using ValueType = void;
    using ErrorType = E;

    [[nodiscard]] static Result Ok() noexcept
    {
        return Result(OkTag{});
    }

    template <typename... Args>
        requires std::is_nothrow_constructible_v<E, Args...>
    [[nodiscard]] static Result Err(Args&&... args) noexcept
    {
        return Result(ErrTag{}, std::forward<Args>(args)...);
    }

    ~Result() noexcept
    {
        Destroy();
    }

    Result(Result&& other) noexcept : m_isOk(other.m_isOk)
    {
        if (!m_isOk)
        {
            std::construct_at(&m_storage.error, std::move(other.m_storage.error));
        }
    }

    Result& operator=(Result&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_isOk = other.m_isOk;
            if (!m_isOk)
            {
                std::construct_at(&m_storage.error, std::move(other.m_storage.error));
            }
        }
        return *this;
    }

    Result(const Result& other) noexcept
        requires(std::is_nothrow_copy_constructible_v<E>)
        : m_isOk(other.m_isOk)
    {
        if (!m_isOk)
        {
            std::construct_at(&m_storage.error, other.m_storage.error);
        }
    }

    Result& operator=(const Result& other) noexcept
        requires(std::is_nothrow_copy_constructible_v<E>)
    {
        if (this != &other)
        {
            Destroy();
            m_isOk = other.m_isOk;
            if (!m_isOk)
            {
                std::construct_at(&m_storage.error, other.m_storage.error);
            }
        }
        return *this;
    }

    [[nodiscard]] bool IsOk() const noexcept
    {
        return m_isOk;
    }

    [[nodiscard]] bool IsError() const noexcept
    {
        return !m_isOk;
    }

    explicit operator bool() const noexcept
    {
        return m_isOk;
    }

    [[nodiscard]] E& Error() & noexcept
    {
        CG_ASSERT(!m_isOk);
        return m_storage.error;
    }

    [[nodiscard]] const E& Error() const& noexcept
    {
        CG_ASSERT(!m_isOk);
        return m_storage.error;
    }

    [[nodiscard]] E&& Error() && noexcept
    {
        CG_ASSERT(!m_isOk);
        return std::move(m_storage.error);
    }

private:
    struct OkTag
    {
    };

    struct ErrTag
    {
    };

    union Storage
    {
        Storage() noexcept {}

        ~Storage() noexcept {}

        E error;
    };

    explicit Result(OkTag) noexcept : m_isOk(true) {}

    template <typename... Args>
    explicit Result(ErrTag, Args&&... args) noexcept : m_isOk(false)
    {
        std::construct_at(&m_storage.error, std::forward<Args>(args)...);
    }

    void Destroy() noexcept
    {
        if (!m_isOk)
        {
            std::destroy_at(&m_storage.error);
        }
    }

    Storage m_storage;
    bool m_isOk;
};

template <typename E>
using Status = Result<void, E>;

}  // namespace cyber::core
