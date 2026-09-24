.syntax unified
.cpu cortex-m3
.thumb

.global Reset_Handler
.global Default_Handler

.extern main
.extern SystemInit

.extern NMI_Handler
.extern HardFault_Handler
.extern MemManage_Handler
.extern BusFault_Handler
.extern UsageFault_Handler
.extern SVC_Handler
.extern DebugMon_Handler
.extern PendSV_Handler
.extern SysTick_Handler

.section .isr_vector, "a", %progbits
.word _estack
.word Reset_Handler
.word NMI_Handler
.word HardFault_Handler
.word MemManage_Handler
.word BusFault_Handler
.word UsageFault_Handler
.word 0
.word 0
.word 0
.word 0
.word SVC_Handler
.word DebugMon_Handler
.word 0
.word PendSV_Handler
.word SysTick_Handler

.section .text.Reset_Handler
.type Reset_Handler, %function

Reset_Handler:
    /* Copy .data from Flash to RAM */
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata

1:
    cmp r1, r2
    bcc 2f
    b 3f

2:
    ldr r3, [r0]
    str r3, [r1]
    adds r0, r0, #4
    adds r1, r1, #4
    b 1b

3:
    /* Clear .bss */
    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0

4:
    cmp r1, r2
    bcc 5f
    b 6f

5:
    str r3, [r1]
    adds r1, r1, #4
    b 4b

6:
    bl SystemInit
    bl main

7:
    b 7b

.size Reset_Handler, .-Reset_Handler


.section .text.Default_Handler
.type Default_Handler, %function

Default_Handler:
    b Default_Handler

.size Default_Handler, .-Default_Handler


/* Weak aliases for interrupt handlers */

.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler

.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler

.weak MemManage_Handler
.thumb_set MemManage_Handler, Default_Handler

.weak BusFault_Handler
.thumb_set BusFault_Handler, Default_Handler

.weak UsageFault_Handler
.thumb_set UsageFault_Handler, Default_Handler

.weak SVC_Handler
.thumb_set SVC_Handler, Default_Handler

.weak DebugMon_Handler
.thumb_set DebugMon_Handler, Default_Handler

.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
