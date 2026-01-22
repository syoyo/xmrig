; Zen 2 optimized AVX2 prefetch for SuperScalar hash
; Uses PREFETCHT0 for all 4 lanes instead of PREFETCHNTA
; Better L3 cache utilization on Zen 2 with 16MB L3 per CCX
	vmovdqu ymmword ptr [rsp], ymm0

	mov rax, [rsp]
	and rax, RANDOMX_CACHE_MASK
	shl rax, 6
	add rax, rdi
	mov [rsp], rax
	prefetcht0 byte ptr [rax]

	mov rax, [rsp+8]
	and rax, RANDOMX_CACHE_MASK
	shl rax, 6
	add rax, rdi
	mov [rsp+8], rax
	prefetcht0 byte ptr [rax]

	mov rax, [rsp+16]
	and rax, RANDOMX_CACHE_MASK
	shl rax, 6
	add rax, rdi
	mov [rsp+16], rax
	prefetcht0 byte ptr [rax]

	mov rax, [rsp+24]
	and rax, RANDOMX_CACHE_MASK
	shl rax, 6
	add rax, rdi
	mov [rsp+24], rax
	prefetcht0 byte ptr [rax]
