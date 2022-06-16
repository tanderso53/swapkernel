	.cpu cortex-m0
	.syntax unified
	.thumb

	.text
	.global isr_hardfault
	.type isr_hardfault, function
	.thumb_func
isr_hardfault:
	bkpt
.isr_hardfault_loop:
	nop
	b	.isr_hardfault_loop
