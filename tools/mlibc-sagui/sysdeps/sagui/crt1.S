.section .text
.global _start

.type _start, %function
.type main, %function
.type __mlibc_entry, %function

.cfi_startproc
_start:
.cfi_undefined eip
	xor %ebp, %ebp
	mov %esp, %ecx

	/* Align the stack to a 16-byte boundary per the i386 psABI. */
	and $-16, %esp

	lea -8(%esp), %esp
	push $main
	push %ecx
	call __mlibc_entry
	ud2
.cfi_endproc

.section .note.GNU-stack,"",%progbits
