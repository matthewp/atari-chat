| uint16_t rsa16_addmul_1(uint16_t *t, const uint16_t *a, unsigned len, uint16_t w)
|
| t[0..len-1] += a[0..len-1] * w, returns the carry limb. The hot loop of
| RSA on the 68000: a 2048-bit Montgomery multiply runs it 256 times over
| 128 limbs. Limbs are least significant first.
|
| Per limb: a[i]*w (MULU.W, 16x16->32) + carry + t[i] always fits in 32
| bits, so the carry into the next limb is just the high word.
|
| Registers: a0 = t, a1 = a, d0 = count, d1/d2 = sum and carry
|            (alternating), d3 = w, d4 = 0 (for ADDX).

	.text
	.even
	.globl	rsa16_addmul_1
	.type	rsa16_addmul_1, @function
rsa16_addmul_1:
	movem.l	%d2-%d4,-(%sp)
	move.l	16(%sp),%a0		| t
	move.l	20(%sp),%a1		| a
	move.l	24(%sp),%d0		| len
	move.w	30(%sp),%d3		| w (low word of the promoted int)
	moveq	#0,%d2			| carry
	moveq	#0,%d4
	lsr.w	#1,%d0			| len/2 pairs; odd limb first if any
	bcc.s	.Lpairs
	| One limb on its own (carry ends up in d2, as the loop expects).
	move.w	(%a1)+,%d2
	mulu.w	%d3,%d2
	add.w	(%a0),%d2
	move.w	%d2,(%a0)+
	clr.w	%d2
	swap	%d2
	addx.w	%d4,%d2
.Lpairs:
	subq.w	#1,%d0
	bmi.s	.Ldone
.Lloop:
	| Unrolled twice; the carry alternates between d2 and d1, which
	| saves a register move per limb.
	move.w	(%a1)+,%d1
	mulu.w	%d3,%d1			| d1 = a[i] * w
	add.l	%d2,%d1			| + carry (cannot overflow)
	add.w	(%a0),%d1		| + t[i], X = carry into the high word
	move.w	%d1,(%a0)+		| store low word (MOVE leaves X alone)
	clr.w	%d1			| CLR and SWAP leave X alone too
	swap	%d1
	addx.w	%d4,%d1			| d1 = carry out

	move.w	(%a1)+,%d2
	mulu.w	%d3,%d2
	add.l	%d1,%d2
	add.w	(%a0),%d2
	move.w	%d2,(%a0)+
	clr.w	%d2
	swap	%d2
	addx.w	%d4,%d2			| d2 = carry out
	dbra	%d0,.Lloop
.Ldone:
	move.l	%d2,%d0
	movem.l	(%sp)+,%d2-%d4
	rts
	.size	rsa16_addmul_1, .-rsa16_addmul_1
