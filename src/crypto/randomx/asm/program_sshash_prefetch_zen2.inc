; Zen 2 optimized prefetch for SuperScalar hash
; Uses PREFETCHT0 instead of PREFETCHNTA for better L3 utilization
; Zen 2 has 16MB L3 per CCX, and RandomX scratchpad (2MB) fits entirely in L3
	and rbx, RANDOMX_CACHE_MASK
	shl rbx, 6
	add rbx, rdi
	prefetcht0 byte ptr [rbx]
