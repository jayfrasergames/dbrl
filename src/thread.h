#ifndef JFG_THREAD_H
#define JFG_THREAD_H

u32 interlocked_compare_exchange(u32 volatile* destination, u32 exchange, u32 comperand);

#ifndef JFG_HEADER_ONLY
// TODO -- platforms other than windows
// should have an ifdef windows here - or ifdef MSVC
#pragma intrinsic(_InterlockedCompareExchange)

u32 interlocked_compare_exchange(u32 volatile* destination, u32 exchange, u32 comperand)
{
	return _InterlockedCompareExchange(destination, exchange, comperand);
}
#endif

#endif
