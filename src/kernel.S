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
	.global _swapk_next
_swapk_next:	.word 0
	.global _swapk_current
_swapk_current:	.word 0
	.text
scb_vtor:	.word SCB_VTOR
vaddr_pendsv:	.word VADDR_PENDSV
nvic_iser:	.word NVIC_ISER
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
/* .swapk_set_handler: ** This doesn't seem to work */
	/* Get vector table location */
/*	ldr	r3, =vaddr_pendsv */
/*	ldr	r3, [r3] */
/*	ldr	r4, =scb_vtor */
/*	ldr	r4, [r4] */
/*	adds	r3, r3, r4 */ /* Add offset for vector */
/*	str	r2, [r3] */ /* store addr to handler in table */
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
/*.swapk_enable_main_save:	
	push	{r0-r3}
	movs	r4, #0
	ldr	r5, =swapk_set_pending
	ldr	r6, =scheduler_ptr
	ldr	r6, [r6]
	mrs	r7, xpsr
	push	{r4-r7}*/
.swapk_startup_exit:
	mov	r0, r2 /* We want to pass sch as arg */
	blx	r1 /* Start entry function passed by C func */
.swapk_startup_return:
	movs	r0, #2
	msr	control, r0 /* Return to MSR */
	pop	{r3-r7} /* Reload regs from calling func */
	mov	r8, r3
	mov	r9, r4
	mov	r10, r5
	mov	r11, r6
	mov	r12, r7
	pop	{r3-r7, pc} /* Branch to calling func */

	.global isr_irq14
	.thumb_func
	.type isr_irq14, function
isr_irq14:
	cpsid	i
	mrs	r0, psp /* We need this to get back home */
	mrs	r1, msp
	msr	msp, r0
	push	{r4-r7, lr} /* Save regs */
	mov	r0, r8
	mov	r1, r9
	mov	r2, r10
	mov	r3, r11
	push	{r0-r3}
	mrs	r0, msp /* Return to msp */
	msr	msp, r1
	ldr	r1, =_swapk_current /* This needs to be pre-loaded */
	ldr	r1, [r1]
	str	r0, [r1] /* Store old stack pointer */
.isr_irq14_load:
	ldr	r0, =_swapk_next /* Pre-load! */
	ldr	r0, [r0]
	ldr	r0, [r0] /* Pointer of a pointer to SP next */
	mrs	r1, msp
	msr	msp, r0
	pop	{r4-r7} /* Reload ISR stack */
	mov	r8, r4
	mov	r9, r5
	mov	r10, r6
	mov	r11, r7
	pop	{r4-r7}
	pop	{r3}
	mrs	r0, msp
	msr	psp, r0 /* New stack pointer for next */
	msr	msp, r1 /* Restore main stack pointer */
	cpsie	i
	b	.isr_irq14_exit
.isr_irq14_unpend:
	push	{r0}
	ldr	r0, =.isr_irq14_exit
	mov	lr, r0
	pop	{r0}
	b	swapk_unset_pending
.isr_irq14_exit:
	bx	r3 /* Branch to unstacking */

	@ swapk_pendsv(void swapk_pendsv(void *newstack:r0,
	@                                void *oldstack:r1)
	@ Handler for pendsv to swap current process
	@
	@ NOTE: This won't work, because the ISR handler is
	@ also pushing and popping things, so we don't know
	@ where the stack frame is
	.global swapk_pendsv
	.thumb_func
	.type swapk_pendsv, function
swapk_pendsv:	
	cpsid	i
	push	{r0, r7}
swapk_pendsv_save:
	mrs	r2, psp
	str	r2, [r0]
	ldr	r0, [r0]
	ldr	r1, [r1]
	cmp	r0, r1
	beq	.swapk_pendsv_exit
	adds	r0, #4
	pop	{r3-r6}
	stm	r0!, {r3-r6}
	pop	{r3-r6}
	stm	r0!, {r3-r6}
.swapk_pendsv_load:
	msr	psp, r1
	adds	r1, #32 /* Get the stack location for next */
	ldmfd	r1!, {r3-r6}
	push	{r3-r6}
	ldmfd	r1!, {r3-r6}
	push	{r3-r6}
	cmp	r5, #0
	bne	.swapk_pendsv_exit
.swapk_pendsv_load_scheduler:
	pop	{r0-r7}
	ldr	r0, =return_msp
	ldr	r0, [r0]
	bx	r0
.swapk_pendsv_exit:
	pop	{r0, r7}
	cpsie i
	bx lr

	@ swapk_wrapper_start()
	@
	@ Wrapper for calling pico functions
	.global swapk_wrapper_start
	.thumb_func
	.type swapk_wrapper_start, function
swapk_wrapper_start:
	mrs	r1, psp
	stm	r1!, {r4-r7}
	movs	r2, 0
	msr	control, r2
	blx	lr

	@ swapk_wrapper_end()
	@
	@ Wrapper for calling pico functions
	.global swapk_wrapper_end
	.thumb_func
	.type swapk_wrapper_end, function
swapk_wrapper_end:
	mrs	r1, psp
	ldm	r1!, {r4-r7}
	movs	r2, 2
	msr	control, r2
	bx	lr

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
	bx	lr

	.global swapk_register_proc
	.thumb_func
	.type swapk_register_proc, function
swapk_register_proc:
	push	{r3-r7}
	mrs	r3, msp
	ldr	r4, [r1]
	msr	msp, r4 /* Use process's stack pointer */
	movs	r4, #0 /* Start with clear registers.                   */
	movs	r5, r0 /* But we could put args in r0, r1 etc in future */
	movs	r6, r0
	ldr	r7, =xpsr_start
	ldr	r7, [r7]
	push	{r4-r7} /* First 4 ISR stack frame */
	movs	r4, #0
	movs	r5, #0
	movs	r6, #0
	movs	r7, #0
	push	{r4-r7} /* Last 4 ISR Stack frame */
	ldr	r4, =return_psp
	ldr	r4, [r4]
	push	{r4} /* ISR LR */
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
	pop	{r3-r7}
	bx	lr

	.global swapk_proc_swap
	.thumb_func
	.type swapk_proc_swap, function
swapk_proc_swap:
	cpsid	i
.swapk_proc_save_state:
	push	{r3-r7} /* Push regs onto stack */
	mov	r3, r8
	mov	r4, r9
	mov	r5, r10
	mov	r6, r11
	mov	r7, r12
	push	{r3-r7}
	push	{lr}
	mrs	r3, psp
	str	r3, [r0] /* Store new stack pointer */
.swapk_proc_load_state:
	ldr	r1, [r1]
	msr	psp, r1
	pop	{r4}
	mov	r0, r4 /* Save LR for later branch op */
	pop	{r3-r7}
	mov	r12, r7
	mov	r11, r6
	mov	r10, r5
	mov	r9, r4
	mov	r8, r3
	pop	{r3, r7} /* Regs restored! */
.swapk_proc_load_exit:
	cpsie	i
	bx	r0
