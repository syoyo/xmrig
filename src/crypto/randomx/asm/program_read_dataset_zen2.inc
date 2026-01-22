; Zen 2 optimized dataset read with PREFETCHT0 for next access
; Dataset is DRAM-backed, but prefetcht0 still provides better latency hiding
; on Zen 2 compared to prefetchnta due to improved prefetcher logic
	mov ecx, ebp                       ;# ecx = ma
	and ecx, RANDOMX_DATASET_BASE_MASK
	xor r8, qword ptr [rdi+rcx]
	ror rbp, 32                        ;# swap "ma" and "mx"
	xor rbp, rax                       ;# modify "mx"
	mov edx, ebp                       ;# edx = mx
	and edx, RANDOMX_DATASET_BASE_MASK
	prefetcht0 byte ptr [rdi+rdx]     ;# Changed from prefetchnta to prefetcht0
	xor r9,  qword ptr [rdi+rcx+8]
	xor r10, qword ptr [rdi+rcx+16]
	xor r11, qword ptr [rdi+rcx+24]
	xor r12, qword ptr [rdi+rcx+32]
	xor r13, qword ptr [rdi+rcx+40]
	xor r14, qword ptr [rdi+rcx+48]
	xor r15, qword ptr [rdi+rcx+56]
