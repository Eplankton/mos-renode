// Fixed-size block allocator shared by the benchmark ports.
//
// MOS has no general-purpose heap, so both suites measure this O(1) freelist
// for their allocation tests. Not thread-safe: the benchmarks allocate from a
// single task at a time.
//
// allocate()/release() are kept out of line on purpose: with LTO the compiler
// otherwise sees an allocate immediately followed by a release, cancels the
// pair and the benchmark ends up timing an empty loop.

#ifndef _MOS_BENCH_BLOCK_POOL_
#define _MOS_BENCH_BLOCK_POOL_

#include <cstddef>
#include <cstdint>

namespace MOS::Bench
{
	template <size_t BLOCK_SIZE, size_t BLOCKS>
	struct BlockPool_t
	{
		static_assert(BLOCK_SIZE >= sizeof(void*), "Block too small");

		static constexpr size_t block_size() { return BLOCK_SIZE; }

		void init()
		{
			free = nullptr;
			for (size_t i = BLOCKS; i > 0; --i) {
				release(&memory[(i - 1) * BLOCK_SIZE]);
			}
		}

		[[gnu::noinline]] void* allocate()
		{
			auto block = free;
			if (block != nullptr) free = *static_cast<void**>(block);
			return block;
		}

		[[gnu::noinline]] void release(void* block)
		{
			*static_cast<void**>(block) = free;
			free                        = block;
		}

	private:
		alignas(8) uint8_t memory[BLOCK_SIZE * BLOCKS];
		void* free = nullptr;
	};
}

#endif
