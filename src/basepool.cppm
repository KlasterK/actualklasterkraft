module;
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <type_traits>
export module actualklasterkraft.basepool;

export template <size_t N> class BasePool
{
public:
    static constexpr size_t MaxSlots = N;
    static_assert(N > 0 && N % 64 == 0);

public:
    size_t allocate()
    {
        for (size_t i { }; i < N / 64; ++i)
        {
            auto &bitmap = m_bitmaps[i];

            int zero_pos = std::countr_one(bitmap);
            if (zero_pos >= 64)
                continue;

            bitmap |= (uint64_t(1) << zero_pos);
            return i * 64 + zero_pos;
        }
        return N;
    }

    void free(size_t idx)
    {
        assert(idx < N);
        m_bitmaps[idx / 64] &= ~(uint64_t(1) << (idx % 64));
    }

    size_t count_taken_slots()
    {
        return std::accumulate(m_bitmaps.begin(), m_bitmaps.end(), 0zu,
            [](size_t sum, uint64_t bitmap)
            { return sum + std::popcount(bitmap); });
    }

protected:
    std::array<uint64_t, N / 64> m_bitmaps { };
};

export template <typename Pool> class PoolTakenSlotsIterator
{
public:
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
    using value_type = Pool::ValueType;
    using difference_type = std::ptrdiff_t;
    using reference = value_type &;
    using pointer = value_type *;

public:
    PoolTakenSlotsIterator() noexcept = default;

    bool operator==(const PoolTakenSlotsIterator &other) const noexcept
    {
        return m_pool == other.m_pool && m_idx == other.m_idx;
    }

    reference operator*() const noexcept
    {
        assert(m_idx < Pool::MaxSlots);
        return m_pool->pool_iterator_dereference(m_idx);
    }

    pointer operator->() const noexcept { return &**this; }

    PoolTakenSlotsIterator &operator++() noexcept
    {
        size_t bitmap_idx = m_idx / 64;
        int bit_idx = m_idx % 64 + 1;

        if (bit_idx == 64)
        {
            ++bitmap_idx;
            bit_idx = 0;
        }

        for (; bitmap_idx < Pool::MaxSlots / 64; ++bitmap_idx)
        {
            uint64_t bitmap = m_pool->m_bitmaps[bitmap_idx];
            bitmap &= ~uint64_t(0) << bit_idx; // clear bits lower than bit_idx

            while (bitmap != 0)
            {
                int one_pos = std::countr_zero(bitmap);

                m_idx = bitmap_idx * 64 + one_pos;
                if (m_pool->pool_iterator_test(m_idx))
                    return *this;

                bitmap &= bitmap - 1; // clear last set bit
            }
            bit_idx = 0;
        }

        m_idx = Pool::MaxSlots;
        return *this;
    }

    PoolTakenSlotsIterator operator++(int) noexcept
    {
        auto tmp = *this;
        ++*this;
        return tmp;
    }

private:
    friend Pool;

    PoolTakenSlotsIterator(Pool &pool, size_t idx)
        : m_pool(&pool)
        , m_idx(idx)
    {
    }

private:
    Pool *m_pool = nullptr;
    size_t m_idx = 0;
};
