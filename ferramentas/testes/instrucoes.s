# Teste do nefs-as: as instruções que o QBE pode gerar e as que o NovaEra usa.
.text
.globl f
f:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $4096, %rsp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	movq $-1, %rax
	movq $0x80000000, %rax
	movabsq $0x123456789abcdef0, %r11
	movl $-1, %r9d
	movw $1000, %ax
	movw $5, %r10w
	movb $7, %sil
	movb $7, %r15b
	movb %ah, %bl
	movb %al, (%rdi)
	movw %si, 2(%rsp)
	movl %eax, -4(%rbp)
	movq %r8, 0x18(%rsp)
	movq %r12, (%r12)
	movq %r13, (%r13)
	movq %rax, (%rax,%rbx,8)
	movq (%r12,%r13,4), %r14
	movq 1000(%rbp,%rcx,2), %rdx
	movl $5, 8(%rdi)
	movq $-5, 8(%rdi)
	movb $1, (%rax)
	movw $300, (%rax)
	movq x(,%rcx,8), %rax
	movq x, %rax
	movl %eax, x
	leaq x(%rip), %rax
	leaq 8(%rax,%rbx,4), %rcx
	leal (%rax,%rax,2), %ecx
	movl $x, %eax
	movq $x+8, %rax
	movq x@GOTPCREL(%rip), %rcx
	movl x+4(%rip), %eax
	movl $3, x(%rip)
	movb $3, x(%rip)
	movw $3, x(%rip)
	movq $3, x(%rip)
	addl $1, %eax
	addl $1000, %eax
	addl $1000, %ecx
	addq $-129, %rsp
	addb $1, %al
	addb $1, %cl
	addw $1, %ax
	addw $1000, %cx
	subl %ecx, (%rdx)
	subq (%rdx), %r9
	andl $0xffffffff, %eax
	andq $-16, %rsp
	orq $0x7fffffff, %rbx
	xorl %eax, %eax
	xorq %r8, %r8
	cmpb $0x41, %al
	cmpb $0x41, %dil
	cmpq $0, (%rax)
	cmpl %eax, %ebx
	adcl $1, %eax
	sbbq %rax, %rax
	testb $1, %al
	testb $1, %cl
	testw $1, %dx
	testq %rax, %rax
	testq $0x100, %rcx
	testl %eax, (%rdi)
	testl (%rdi), %eax
	shll %cl, %eax
	shlq $3, %rax
	shrq $1, %rbx
	sarl %cl, %r10d
	sarw $2, %ax
	shrb $1, %al
	shlb %cl, (%rax)
	sall $4, %eax
	rolq $5, %rax
	rorl %cl, %ecx
	imull %ecx, %eax
	imulq (%rdi), %rax
	imull $3, %eax
	imulq $1000, %rcx
	imulq $-2, %r8, %r9
	imulw %cx, %ax
	imulq %rbx
	mulq %rcx
	divl %ecx
	idivq (%rdi)
	divb %cl
	negq %rax
	negl %r12d
	notq %rax
	incl %eax
	decq (%rax)
	incb %al
	cqto
	cltd
	cltq
	cwtl
	movzbl %al, %eax
	movzbl %sil, %eax
	movzbq %dil, %rax
	movzbw %al, %ax
	movzwl %ax, %eax
	movzwq (%rdi), %rax
	movsbl %al, %eax
	movsbq (%rdi), %rax
	movsbw %al, %ax
	movswl %ax, %eax
	movswq %r8w, %r9
	movslq %eax, %rax
	movslq (%rdi), %r10
	movzbl (%rax), %ecx
	setz %al
	setnz %sil
	setb %r8b
	setae (%rax)
	setl %al
	setge %al
	setle %al
	setg %al
	seta %al
	setbe %al
	setp %al
	setnp %al
	sete %bl
	cmovz %rax, %rbx
	cmovnz %eax, %ebx
	cmovl (%rdi), %rax
	cmovge %r8, %r9
	cmova %ecx, %edx
	cmovbe %rcx, %rdx
	cmovae %rcx, %rdx
	cmovb %rcx, %rdx
	cmovle %rcx, %rdx
	cmovg %rcx, %rdx
	xchgq %rax, %rbx
	xchgl %eax, %ecx
	xchgl %eax, %eax
	xchgq %r8, %rax
	xchgq %r9, %r10
	xchgl %ecx, (%rdi)
	xchgb %al, %bl
	pushq $5
	pushq $1000
	pushq (%rax)
	pushq %r15
	popq %r15
	popq (%rax)
	callq g
	callq *%rax
	callq *%r11
	callq *(%rax)
	callq *8(%rbx)
	callq f
	jmp *%rax
	jmp *(%rax,%rcx,8)
	movss %xmm0, %xmm1
	movss (%rax), %xmm0
	movss %xmm0, (%rax)
	movsd %xmm8, %xmm15
	movsd 8(%rsp), %xmm9
	movsd %xmm2, -16(%rbp)
	movsd .Lc(%rip), %xmm0
	movaps %xmm0, -176(%rbp)
	movaps %xmm9, -160(%rbp)
	movq %xmm0, %rax
	movq %rax, %xmm0
	movq %r9, %xmm12
	movq %xmm12, %r9
	movq %xmm1, %xmm2
	movq (%rax), %xmm3
	movq %xmm3, (%rax)
	movd %eax, %xmm0
	movd %xmm0, %eax
	addss %xmm1, %xmm0
	addsd (%rax), %xmm0
	subss %xmm1, %xmm0
	subsd %xmm1, %xmm0
	mulss %xmm1, %xmm0
	mulsd %xmm10, %xmm0
	divss %xmm1, %xmm0
	divsd %xmm1, %xmm11
	sqrtsd %xmm1, %xmm0
	ucomiss %xmm1, %xmm0
	ucomisd %xmm1, %xmm0
	ucomisd (%rax), %xmm8
	comisd %xmm1, %xmm0
	cvtss2sd %xmm1, %xmm0
	cvtsd2ss %xmm1, %xmm0
	cvttss2si %xmm0, %eax
	cvttss2si %xmm0, %rax
	cvttsd2si %xmm0, %eax
	cvttsd2si %xmm8, %r8
	cvttsd2siq %xmm0, %rax
	cvttsd2sil %xmm0, %eax
	cvttss2siq %xmm0, %rax
	cvttss2sil %xmm0, %eax
	cvtsi2ss %eax, %xmm0
	cvtsi2ss %rax, %xmm0
	cvtsi2sd %eax, %xmm0
	cvtsi2sd %rax, %xmm0
	cvtsi2sdl (%rax), %xmm0
	cvtsi2sdq (%rax), %xmm0
	cvtsi2ssq %r9, %xmm9
	cvtsi2sdl %r9d, %xmm9
	xorps %xmm0, %xmm0
	xorpd %xmm1, %xmm1
	pxor %xmm2, %xmm2
	andpd %xmm1, %xmm2
	ud2
	nop
	hlt
	leave
	ret
.L1:
	jz .L1
	jnz .L2
	jo .L2
	jno .L2
	js .L2
	jns .L2
	jp .L2
	jnp .L2
	jl .L2
	jge .L2
	jle .L2
	jg .L2
	jb .L2
	jae .L2
	jbe .L2
	ja .L2
	jmp .L2
	jmp g
	jz g
	jmp ext
	jz ext
	.fill 200,1,0x90
.L2:
	jmp .L1
	ret
g:	ret
.Lc:
	.quad 0x4000000000000000
.data
.balign 8
x:	.quad 1, -1, x, g, .L1, ext+8
	.int 1, 2
	.short 1, 0xffff
	.byte 1, 2, 255, -1
	.ascii "ola\n\t\"\\\033\0x"
	.asciz "fim"
	.string "fim2"
	.balign 16
	.fill 3, 4, 0x01020304
	.zero 5
	.int 1078530011 /* 3.14 */
	.quad x - .
.section .rodata
	.int .L1 - f
.bss
.balign 32
y:	.fill 100,1,0
	.zero 7
.comm z, 16, 8
.section .multiboot,"a",@progbits
	.long 0x1BADB002
.section .note.GNU-stack,"",@progbits

# Saltos para símbolos globais e fracos, e inc/dec curtos do modo de 32 bits
.text
.globl a
.weak w
a: jz a
jmp a
jmp w
jz w
w: ret
.code32
incl %ecx
decw %ax
incb %al
incl (%eax)
