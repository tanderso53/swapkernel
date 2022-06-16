#define NVIC_ISER 0xe000e100
#define NVIC_ICER 0xe000e180
#define NVIC_ISPR 0xe000e200
#define NVIC_ICPR 0xe000e280

#define SCB_VTOR 0xe000ed08

#define VADDR_PENDSV 0x00000038

	.cpu cortex-m0
	.syntax unified
	.thumb

	.data
	.global scheduler_ptr
scheduler_ptr:	.word 0
	.weak _swapk_next
_swapk_next:	.word 0
	.weak _swapk_current
_swapk_current:	.word 0

	.text
scb_vtor:	.word SCB_VTOR
vaddr_pendsv:	.word VADDR_PENDSV
nvic_iser:	.word NVIC_ISER
nvic_icer:	.word NVIC_ICER
nvic_ispr:	.word NVIC_ISPR
nvic_icpr:	.word NVIC_ICPR
return_psp:	.word 0xfffffffd
return_msp:	.word 0xfffffff9
xpsr_start:	.word 0x01000000

	.global swapk_startup
	.thumb_func
	.type swapk_startup, function
swapk_startup:
	push	{r3-r7, lr} /* Prepare to return just in case */
	mov	r3, r8
	mov	r4, r9
	mov	r5, r10
	mov	r6, r11
	mov	r7, r12
	push	{r3-r7}
	ldr	r3, =.swapk_startup_return
	mov	lr, r3
	msr	psp, r0 /* Switch to proces SP */
	movs	r4, #2
	mrs	r3, control
	orrs	r3, r3, r4
	msr	control, r3 /* Set proc to use PSP */
	isb
	bl	.swapk_enable_handler
	bl	.swapk_startup_exit
.swapk_enable_handler:
	push	{r2-r4}
	ldr	r4, =nvic_iser
	ldr	r4, [r4]
	ldr	r2, [r4]
	movs	r3, #1
	lsls	r3, r3, #14 /* PENDSV is exception 14 */
	orrs	r2, r2, r3
	str	r2, [r4]
	pop	{r2-r4}
	bx	lr
.swapk_startup_exit:
	mov	r0, r2 /* We want to pass sch as arg */
	blx	r1 /* Start entry function passed by C func */
.swapk_startup_return:
	movs	r0, #0
	msr	control, r0 /* Return to MSR */
	isb
	pop	{r3-r7} /* Reload regs from calling func */
	mov	r8, r3
	mov	r9, r4
	mov	r10, r5
	mov	r11, r6
	mov	r12, r7
	pop	{r3-r7, pc} /* Branch to calling func */

	.global swapk_svc_enable
	.thumb_func
	.type swapk_svc_enable, function
swapk_svc_enable:
	push	{r0-r1, lr}
	ldr	r0, =nvic_iser
	b	.swapk_svc_set_reg

	.global swapk_svc_disable
	.thumb_func
	.type swapk_svc_disable, function
swapk_svc_disable:
	push	{r0-r1, lr}
	ldr	r0, =nvic_icer
.swapk_svc_set_reg:
	ldr	r0, [r0]
	movs	r1, #1
	lsls	r1, r1, #11 /* SVC is exception 11 */
	str	r1, [r0]
	pop	{r0-r1, pc}

	/* void swapk_pendsv_swap(void **current, void **next) */
	.global swapk_pendsv_swap
	.thumb_func
	.type swapk_pendsv_swap, function
swapk_pendsv_swap:
	cpsid	i
	push	{r2-r3, lr}
/* Store the current regs */
	mrs	r2, psp /* We need this to get back home */
	mov	r3, sp
	mov	sp, r2
	push	{r4-r7} /* Save regs */
	mov	r4, r8
	mov	r5, r9
	mov	r6, r10
	mov	r7, r11
	push	{r4-r7}
	mov	r2, sp /* Return to msp */
	mov	sp, r3
	str	r2, [r0] /* Store old stack pointer */
/* Load the next regs */
	ldr	r1, [r1] /* Pointer of a pointer to SP next */
	mov	r3, sp
	mov	sp, r1
	pop	{r4-r7} /* Reload ISR stack */
	mov	r8, r4
	mov	r9, r5
	mov	r10, r6
	mov	r11, r7
	pop	{r4-r7}
	mov	r1, sp
	msr	psp, r1 /* New stack pointer for next */
	msr	msp, r3 /* Restore main stack pointer */
	isb
	bl	swapk_svc_enable /* Allow SVC if one is pending */
/* All done! Let's exit. */
	cpsie	i
	pop	{r2-r3, pc} /* Branch to return */

	@ swapk_set_pending()
	@
	@ Signal processor to perform a context switch
	.global swapk_set_pending
	.thumb_func
	.type swapk_set_pending, function
swapk_set_pending:
	push	{r0-r2}
	ldr	r0, =nvic_ispr
	b	.swapk_nvic_write
swapk_unset_pending:
	push	{r0-r2}
	ldr	r0, =nvic_icpr
.swapk_nvic_write:
	ldr	r0, [r0]
	/* ldr	r1, [r0] */
	movs	r2, #1
	lsls	r2, r2, #14 /* PENSV is exception no 14 */
	/* orrs	r1, r1, r2 */
	str	r2, [r0]
	pop	{r0-r2}
	dsb
	bx	lr

	.global swapk_svc_pend
	.thumb_func
	.type swapk_svc_pend, function
swapk_svc_pend:
	push	{r0-r1, lr}
	ldr	r0, =nvic_ispr
	ldr	r0, [r0]
	movs	r1, #1
	lsls	r1, r1, #11 /* SVC is exception 11 */
	str	r1, [r0]
	pop	{r0-r1, pc}

	/*
	 * void swapk_register_proc(void *entry, void *stack,
	 *                          void *end, void *arg)
	 */
	.global swapk_register_proc
	.thumb_func
	.type swapk_register_proc, function
swapk_register_proc:
	push	{r4-r7}
	mov	r4, r8
	push	{r4}
	mov	r8, r3
	mrs	r3, msp
	ldr	r4, [r1]
	msr	msp, r4 /* Use process's stack pointer */
	isb
	movs	r4, #0 /* Start with clear registers.                   */
	movs	r5, r2 /* But we could put args in r0, r1 etc in future */
	movs	r6, r0
	ldr	r7, =xpsr_start
	ldr	r7, [r7]
	push	{r4-r7} /* First 4 ISR stack frame */
	mov	r4, r8 /* arg to pass on entry */
	movs	r5, #0
	movs	r6, #0
	movs	r7, #0
	push	{r4-r7} /* Last 4 ISR Stack frame */
	movs	r4, #0
	movs	r5, #0
	movs	r6, #0
	movs	r7, #0
	push	{r4-r7} /* First 4 context frame */
	movs	r4, #0
	movs	r5, #0
	movs	r6, #0
	movs	r7, #0
	push	{r4-r7} /* Last 4 context frame */
	mrs	r4, msp
	str	r4, [r1] /* Store stack pointer so we can get back here */
	msr	msp, r3 /* Restore sp */
	isb
	pop	{r4}
	mov	r8, r4
	pop	{r4-r7}
	bx	lr

	/* void swapk_isr_arg(int argc [r0], void **argv [r1],
	 *                    void *ptr [r2]) */
	.global swapk_isr_arg
	.type swapk_isr_arg, function
	.thumb_func
swapk_isr_arg:
	push	{r3, lr}
.swapk_isr_arg_loop:
	cmp	r0, #0
	beq	.swapk_isr_arg_exit
	ldr	r3, [r1]
	push	{r3}
	subs	r0, #1
	adds	r1, #4
	b	.swapk_isr_arg_loop
.swapk_isr_arg_exit:
	mov	r0, sp
	str	r0, [r2]
	pop	{r3, pc}
