#include <stdint.h>

extern "C" long __sagui_syscall0(long n) {
	long r;
	__asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n) : "memory");
	return r;
}

extern "C" long __sagui_syscall1(long n, long a) {
	long r;
	register long ebx __asm__("ebx") = a;
	__asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "r"(ebx) : "memory");
	return r;
}

extern "C" long __sagui_syscall2(long n, long a, long b) {
	long r;
	register long ebx __asm__("ebx") = a;
	register long ecx __asm__("ecx") = b;
	__asm__ __volatile__("int $0x80"
			: "=a"(r)
			: "a"(n), "r"(ebx), "r"(ecx)
			: "memory");
	return r;
}

extern "C" long __sagui_syscall3(long n, long a, long b, long c) {
	long r;
	register long ebx __asm__("ebx") = a;
	register long ecx __asm__("ecx") = b;
	register long edx __asm__("edx") = c;
	__asm__ __volatile__("int $0x80"
			: "=a"(r)
			: "a"(n), "r"(ebx), "r"(ecx), "r"(edx)
			: "memory");
	return r;
}

extern "C" long __sagui_syscall4(long n, long a, long b, long c, long d) {
	long r;
	register long ebx __asm__("ebx") = a;
	register long ecx __asm__("ecx") = b;
	register long edx __asm__("edx") = c;
	register long esi __asm__("esi") = d;
	__asm__ __volatile__("int $0x80"
			: "=a"(r)
			: "a"(n), "r"(ebx), "r"(ecx), "r"(edx), "r"(esi)
			: "memory");
	return r;
}

extern "C" long __sagui_syscall5(long n, long a, long b, long c, long d, long e) {
	long r;
	register long ebx __asm__("ebx") = a;
	register long ecx __asm__("ecx") = b;
	register long edx __asm__("edx") = c;
	register long esi __asm__("esi") = d;
	register long edi __asm__("edi") = e;
	__asm__ __volatile__("int $0x80"
			: "=a"(r)
			: "a"(n), "r"(ebx), "r"(ecx), "r"(edx), "r"(esi), "r"(edi)
			: "memory");
	return r;
}
