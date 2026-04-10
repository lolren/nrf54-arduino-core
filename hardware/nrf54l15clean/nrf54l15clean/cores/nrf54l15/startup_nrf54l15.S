/*
 * Startup File for nRF54L15 (Cortex-M33)
 *
 * This file contains:
 * - Interrupt vector table
 * - Reset handler (copies .data, zeroes .bss, calls main)
 * - Default exception/interrupt handlers
 *
 * Licensed under the Apache License 2.0
 */

    .syntax unified
    .cpu cortex-m33
    .thumb
    .fpu softvfp

    .global __stack_start__
    .global g_pfnVectors
    .extern __libc_init_array

/* *****************************************************************************
 * Vector Table
 * *****************************************************************************/

    .section .isr_vector, "a"
    .align 4
    .type g_pfnVectors, %object
    .size g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    .word   __stack_start__         /* 0: Initial Stack Pointer */
    .word   Reset_Handler           /* 1: Reset Handler */
    .word   NMI_Handler             /* 2: NMI Handler */
    .word   HardFault_Handler       /* 3: Hard Fault Handler */
    .word   MemManage_Handler       /* 4: MPU Fault Handler */
    .word   BusFault_Handler        /* 5: Bus Fault Handler */
    .word   UsageFault_Handler      /* 6: Usage Fault Handler */
    .word   SecureFault_Handler     /* 7: Secure Fault Handler */
    .word   0                       /* 8: Reserved */
    .word   0                       /* 9: Reserved */
    .word   0                       /* 10: Reserved */
    .word   SVC_Handler             /* 11: SVCall Handler */
    .word   DebugMon_Handler        /* 12: Debug Monitor Handler */
    .word   0                       /* 13: Reserved */
    .word   PendSV_Handler          /* 14: PendSV Handler */
    .word   SysTick_Handler         /* 15: SysTick Handler */

    /* External Interrupts (nRF54L15 specific). */
    /* IRQ 0..69 */
    .rept 70
    .word   Default_Handler
    .endr
    /* IRQ 70..71: AAR00/CCM00 and ECB00. */
    .word   AAR00_CCM00_IRQHandler
    .word   ECB00_IRQHandler
    /* IRQ 72..73 */
    .rept 2
    .word   Default_Handler
    .endr
    /* IRQ 74: serial-fabric instance 00. */
    .word   SPIM00_IRQHandler
    /* IRQ 75..134 */
    .rept 60
    .word   Default_Handler
    .endr
    /* IRQ 135: EGU10. */
    .word   EGU10_IRQHandler
    /* IRQ 136..139 */
    .rept 4
    .word   Default_Handler
    .endr
    /* IRQ 140..141: serial-fabric instances used by HardwareSerial. */
    .word   SPIM20_IRQHandler
    .word   SPIM21_IRQHandler
    /* IRQ 142..148 */
    .rept 7
    .word   Default_Handler
    .endr
    /* IRQ 149..150: TWIM/TWIS instances used by Wire target IRQ mode. */
    .word   TWIM20_IRQHandler
    .word   TWIM21_IRQHandler
    /* IRQ 151..199 */
    .rept 49
    .word   Default_Handler
    .endr
    /* IRQ 200: serial-fabric instance 22. */
    .word   SPIM22_IRQHandler
    /* IRQ 201: EGU20. */
    .word   EGU20_IRQHandler
    /* IRQ 202..217 */
    .rept 16
    .word   Default_Handler
    .endr
    /* IRQ 218..219: GPIOTE20 IRQ lines used by attachInterrupt(). */
    .word   GPIOTE20_0_IRQHandler
    .word   GPIOTE20_1_IRQHandler
    /* IRQ 220 */
    .word   Default_Handler
    /* IRQ 221: I2S20 */
    .word   I2S20_IRQHandler
    /* IRQ 222..225 */
    .rept 4
    .word   Default_Handler
    .endr
    /* IRQ 226..228: GRTC interrupt groups. */
    .word   GRTC_0_IRQHandler
    .word   GRTC_1_IRQHandler
    .word   GRTC_2_IRQHandler
    /* IRQ 229..259 */
    .rept 31
    .word   Default_Handler
    .endr
    /* IRQ 260: serial-fabric instance 30. */
    .word   SPIM30_IRQHandler
    /* IRQ 261: CLOCK_POWER */
    .word   CLOCK_POWER_IRQHandler
    /* IRQ 262: LPCOMP */
    .word   LPCOMP_IRQHandler
    /* IRQ 263..269 */
    .rept 7
    .word   Default_Handler
    .endr

/* *****************************************************************************
 * Reset Handler
 * *****************************************************************************/

    .section .text.Reset_Handler
    .global Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    /* Disable interrupts */
    cpsid i

    /* Copy .data section from FLASH (sidata) to RAM (sdata..edata). */
    ldr r0, =_sidata         /* SRC: .data load address in FLASH */
    ldr r1, =_sdata          /* DST: .data start in RAM */
    ldr r2, =_edata          /* DST: .data end in RAM */

LoopCopyData:
    cmp r1, r2
    bcs LoopCopyDataDone
    ldr r3, [r0], #4
    str r3, [r1], #4
    b LoopCopyData

LoopCopyDataDone:
    /* Zero out .bss section (sbss..ebss). */
    ldr r0, =_sbss
    ldr r1, =_ebss
    mov r2, #0

LoopClearBss:
    cmp r0, r1
    bcs RunMain
    str r2, [r0], #4
    b LoopClearBss

RunMain:
    /* Configure clocks/system state if overridden by the core. */
    bl SystemInit

    /* Run C/C++ runtime init so global constructors are executed. */
    bl __libc_init_array

    /* Enable interrupts */
    cpsie i

    /* Call main() */
    bl main

    /* main() should never return, but if it does, loop forever */
InfiniteLoop:
    b InfiniteLoop

    .size Reset_Handler, .-Reset_Handler

/* *****************************************************************************
 * Exception Handlers
 * *****************************************************************************/

    .weak NMI_Handler
    .type NMI_Handler, %function
NMI_Handler:
    b .
    .size NMI_Handler, .-NMI_Handler

    .weak HardFault_Handler
    .type HardFault_Handler, %function
HardFault_Handler:
    b .
    .size HardFault_Handler, .-HardFault_Handler

    .weak MemManage_Handler
    .type MemManage_Handler, %function
MemManage_Handler:
    b .
    .size MemManage_Handler, .-MemManage_Handler

    .weak BusFault_Handler
    .type BusFault_Handler, %function
BusFault_Handler:
    b .
    .size BusFault_Handler, .-BusFault_Handler

    .weak UsageFault_Handler
    .type UsageFault_Handler, %function
UsageFault_Handler:
    b .
    .size UsageFault_Handler, .-UsageFault_Handler

    .weak SecureFault_Handler
    .type SecureFault_Handler, %function
SecureFault_Handler:
    b .
    .size SecureFault_Handler, .-SecureFault_Handler

    .weak SVC_Handler
    .type SVC_Handler, %function
SVC_Handler:
    b .
    .size SVC_Handler, .-SVC_Handler

    .weak DebugMon_Handler
    .type DebugMon_Handler, %function
DebugMon_Handler:
    b .
    .size DebugMon_Handler, .-DebugMon_Handler

    .weak PendSV_Handler
    .type PendSV_Handler, %function
PendSV_Handler:
    b .
    .size PendSV_Handler, .-PendSV_Handler

    /* SysTick_Handler is provided by wiring_time.c */

/* *****************************************************************************
 * Default Interrupt Handler
 * *****************************************************************************/

    .weak AAR00_CCM00_IRQHandler
    .type AAR00_CCM00_IRQHandler, %function
AAR00_CCM00_IRQHandler:
    bx lr
    .size AAR00_CCM00_IRQHandler, .-AAR00_CCM00_IRQHandler

    .weak ECB00_IRQHandler
    .type ECB00_IRQHandler, %function
ECB00_IRQHandler:
    bx lr
    .size ECB00_IRQHandler, .-ECB00_IRQHandler

    .weak SPIM00_IRQHandler
    .type SPIM00_IRQHandler, %function
SPIM00_IRQHandler:
    bx lr
    .size SPIM00_IRQHandler, .-SPIM00_IRQHandler

    .weak EGU10_IRQHandler
    .type EGU10_IRQHandler, %function
EGU10_IRQHandler:
    bx lr
    .size EGU10_IRQHandler, .-EGU10_IRQHandler

    .weak TWIM20_IRQHandler
    .type TWIM20_IRQHandler, %function
TWIM20_IRQHandler:
    b .
    .size TWIM20_IRQHandler, .-TWIM20_IRQHandler

    .weak TWIM21_IRQHandler
    .type TWIM21_IRQHandler, %function
TWIM21_IRQHandler:
    b .
    .size TWIM21_IRQHandler, .-TWIM21_IRQHandler

    .weak GRTC_0_IRQHandler
    .type GRTC_0_IRQHandler, %function
GRTC_0_IRQHandler:
    b .
    .size GRTC_0_IRQHandler, .-GRTC_0_IRQHandler

    .weak I2S20_IRQHandler
    .type I2S20_IRQHandler, %function
I2S20_IRQHandler:
    b .
    .size I2S20_IRQHandler, .-I2S20_IRQHandler

    .weak SPIM22_IRQHandler
    .type SPIM22_IRQHandler, %function
SPIM22_IRQHandler:
    bx lr
    .size SPIM22_IRQHandler, .-SPIM22_IRQHandler

    .weak EGU20_IRQHandler
    .type EGU20_IRQHandler, %function
EGU20_IRQHandler:
    bx lr
    .size EGU20_IRQHandler, .-EGU20_IRQHandler

    .weak GRTC_1_IRQHandler
    .type GRTC_1_IRQHandler, %function
GRTC_1_IRQHandler:
    b .
    .size GRTC_1_IRQHandler, .-GRTC_1_IRQHandler

    .weak GRTC_2_IRQHandler
    .type GRTC_2_IRQHandler, %function
GRTC_2_IRQHandler:
    b .
    .size GRTC_2_IRQHandler, .-GRTC_2_IRQHandler

    .weak CLOCK_POWER_IRQHandler
    .type CLOCK_POWER_IRQHandler, %function
CLOCK_POWER_IRQHandler:
    b .
    .size CLOCK_POWER_IRQHandler, .-CLOCK_POWER_IRQHandler

    .weak LPCOMP_IRQHandler
    .type LPCOMP_IRQHandler, %function
LPCOMP_IRQHandler:
    b .
    .size LPCOMP_IRQHandler, .-LPCOMP_IRQHandler

    .weak SPIM30_IRQHandler
    .type SPIM30_IRQHandler, %function
SPIM30_IRQHandler:
    bx lr
    .size SPIM30_IRQHandler, .-SPIM30_IRQHandler

    .weak Default_Handler
    .type Default_Handler, %function
Default_Handler:
    b .
    .size Default_Handler, .-Default_Handler

/* *****************************************************************************
 * System Init (optional - can be overridden)
 * *****************************************************************************/

    .weak SystemInit
    .type SystemInit, %function
SystemInit:
    bx lr
    .size SystemInit, .-SystemInit
