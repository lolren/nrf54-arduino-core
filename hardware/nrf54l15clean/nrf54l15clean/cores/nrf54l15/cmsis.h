/*
 * CMSIS-Core Compatibility Header for nRF54L15
 *
 * Minimal CMSIS-Core definitions for ARM Cortex-M33.
 * Based on ARM CMSIS-CORE specification.
 *
 * Licensed under the Apache License 2.0
 */

#ifndef CMSIS_H
#define CMSIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// ============================================================================
// Processor and Core Peripheral Section
// ============================================================================

#define __CM33_REV              0x0004U    /*!< Core revision r0p4 */
#define __DSP_PRESENT           1          /*!< DSP present */
#define __FPU_PRESENT           1          /*!< FPU present */
#define __NVIC_PRIO_BITS        3          /*!< Interrupt priority bits */
#define __VTOR_PRESENT          1          /*!< VTOR present */
#define __MPU_PRESENT           1          /*!< MPU present */

// ============================================================================
// NVIC Register Structure
// ============================================================================

typedef struct {
    volatile uint32_t ISER[9U];           /*!< Interrupt Set Enable Register */
    uint32_t RESERVED0[24U];
    volatile uint32_t ICER[9U];           /*!< Interrupt Clear Enable Register */
    uint32_t RESERVED1[24U];
    volatile uint32_t ISPR[9U];           /*!< Interrupt Set Pending Register */
    uint32_t RESERVED2[24U];
    volatile uint32_t ICPR[9U];           /*!< Interrupt Clear Pending Register */
    uint32_t RESERVED3[24U];
    volatile uint32_t IABR[9U];           /*!< Interrupt Active Bit Register */
    uint32_t RESERVED4[56U];
    volatile uint8_t  IP[270U];           /*!< Interrupt Priority Register */
    uint32_t RESERVED5[644U];
    volatile uint32_t STIR;               /*!< Software Trigger Interrupt Register */
} NVIC_Type;

#define NVIC_BASE           0xE000E100UL
#define NVIC                ((NVIC_Type *) NVIC_BASE)

// ============================================================================
// SysTick Register Structure
// ============================================================================

typedef struct {
    volatile uint32_t CTRL;               /*!< Control Register */
    volatile uint32_t LOAD;               /*!< Reload Register */
    volatile uint32_t VAL;                /*!< Current Register */
    volatile uint32_t CALIB;              /*!< Calibration Register */
} SysTick_Type;

#define SysTick_BASE        0xE000E010UL
#define SysTick             ((SysTick_Type *) SysTick_BASE)

// ============================================================================
// SysTick Control Register Definitions
// ============================================================================

#define SysTick_CTRL_COUNTFLAG_Pos         16U
#define SysTick_CTRL_COUNTFLAG_Msk         (1UL << SysTick_CTRL_COUNTFLAG_Pos)
#define SysTick_CTRL_CLKSOURCE_Pos         2U
#define SysTick_CTRL_CLKSOURCE_Msk         (1UL << SysTick_CTRL_CLKSOURCE_Pos)
#define SysTick_CTRL_TICKINT_Pos           1U
#define SysTick_CTRL_TICKINT_Msk           (1UL << SysTick_CTRL_TICKINT_Pos)
#define SysTick_CTRL_ENABLE_Pos            0U
#define SysTick_CTRL_ENABLE_Msk            (1UL << SysTick_CTRL_ENABLE_Pos)

// ============================================================================
// Interrupt Numbers (must be declared before NVIC functions that use IRQn_Type)
// ============================================================================

typedef enum IRQn {
    // Core interrupts
    Reset_IRQn             = -15,
    NonMaskableInt_IRQn    = -14,
    HardFault_IRQn         = -13,
    MemoryManagement_IRQn  = -12,
    BusFault_IRQn          = -11,
    UsageFault_IRQn        = -10,
    SecureFault_IRQn       =  -9,
    SVCall_IRQn            =  -5,
    DebugMonitor_IRQn      =  -4,
    PendSV_IRQn            =  -2,
    SysTick_IRQn           =  -1,

    // Peripheral interrupts
    AAR00_CCM00_IRQn      = 70,
    ECB00_IRQn            = 71,
    SPIM00_IRQn           = 74,
    EGU10_IRQn            = 135,
    SPIM20_IRQn            = 140,
    SPIM21_IRQn            = 141,
    TWIM20_IRQn            = 149,
    TWIM21_IRQn            = 150,
    SPIM22_IRQn            = 200,
    EGU20_IRQn             = 201,
    TIMER20_IRQn           = 202,
    TIMER21_IRQn           = 203,
    TIMER22_IRQn           = 204,
    TIMER23_IRQn           = 205,
    TIMER24_IRQn           = 206,
    PDM20_IRQn             = 208,
    PDM21_IRQn             = 209,
    PWM20_IRQn             = 210,
    PWM21_IRQn             = 211,
    PWM22_IRQn             = 212,
    SAADC_IRQn             = 157,
    NFCT_IRQn              = 214,
    TEMP_IRQn              = 215,
    GPIOTE20_0_IRQn        = 218,
    GPIOTE20_1_IRQn        = 219,
    I2S20_IRQn             = 221,
    QDEC20_IRQn            = 224,
    QDEC21_IRQn            = 225,
    GRTC_0_IRQn            = 226,
    GRTC_1_IRQn            = 227,
    GRTC_2_IRQn            = 228,
    GRTC_3_IRQn            = 229,
    SPIM30_IRQn            = 260,
    CLOCK_POWER_IRQn       = 261,
    LPCOMP_IRQn            = 262,
    WDT30_IRQn             = 264,
    WDT31_IRQn             = 265,
    GPIOTE30_0_IRQn        = 268,
    GPIOTE30_1_IRQn        = 269,
} IRQn_Type;

#define AAR00_IRQn AAR00_CCM00_IRQn
#define CCM00_IRQn AAR00_CCM00_IRQn
#define UARTE00_IRQn SPIM00_IRQn
#define UARTE20_IRQn SPIM20_IRQn
#define UARTE21_IRQn SPIM21_IRQn
#define TWIS20_IRQn TWIM20_IRQn
#define TWIS21_IRQn TWIM21_IRQn
#define TWIM22_IRQn SPIM22_IRQn
#define TWIS22_IRQn SPIM22_IRQn
#define UARTE22_IRQn SPIM22_IRQn
#define TWIM30_IRQn SPIM30_IRQn
#define TWIS30_IRQn SPIM30_IRQn
#define UARTE30_IRQn SPIM30_IRQn
#define CLOCK_IRQn CLOCK_POWER_IRQn
#define POWER_IRQn CLOCK_POWER_IRQn
#define COMP_IRQn LPCOMP_IRQn

// ============================================================================
// Core Function Helpers
// ============================================================================

// Enable global interrupts
static inline void __enable_irq(void)
{
    __asm__ volatile ("cpsie i" : : : "memory");
}

// Disable global interrupts
static inline void __disable_irq(void)
{
    __asm__ volatile ("cpsid i" : : : "memory");
}

// Get Control Register
static inline uint32_t __get_CONTROL(void)
{
    uint32_t result;
    __asm__ volatile ("MRS %0, control" : "=r" (result) );
    return result;
}

// Set Control Register
static inline void __set_CONTROL(uint32_t control)
{
    __asm__ volatile ("MSR control, %0" : : "r" (control) : "memory");
}

// Get IPSR Register
static inline uint32_t __get_IPSR(void)
{
    uint32_t result;
    __asm__ volatile ("MRS %0, ipsr" : "=r" (result) );
    return result;
}

// Get Priority Mask
static inline uint32_t __get_PRIMASK(void)
{
    uint32_t result;
    __asm__ volatile ("MRS %0, primask" : "=r" (result) );
    return result;
}

// Set Priority Mask
static inline void __set_PRIMASK(uint32_t priMask)
{
    __asm__ volatile ("MSR primask, %0" : : "r" (priMask) : "memory");
}

// Get Base Priority Mask
static inline uint32_t __get_BASEPRI(void)
{
    uint32_t result;
    __asm__ volatile ("MRS %0, basepri" : "=r" (result) );
    return result;
}

// Set Base Priority Mask
static inline void __set_BASEPRI(uint32_t value)
{
    __asm__ volatile ("MSR basepri, %0" : : "r" (value) : "memory");
}

// Set Base Priority Mask (MAX)
static inline void __set_BASEPRI_MAX(uint32_t value)
{
    __asm__ volatile ("MSR basepri_max, %0" : : "r" (value) : "memory");
}

// No Operation
static inline void __NOP(void)
{
    __asm__ volatile ("nop");
}

// Wait For Interrupt
static inline void __WFI(void)
{
    __asm__ volatile ("wfi");
}

// Wait For Event
static inline void __WFE(void)
{
    __asm__ volatile ("wfe");
}

// Data Synchronization Barrier
static inline void __DSB(void)
{
    __asm__ volatile ("dsb 0xF" ::: "memory");
}

// Data Memory Barrier
static inline void __DMB(void)
{
    __asm__ volatile ("dmb 0xF" ::: "memory");
}

// Instruction Synchronization Barrier
static inline void __ISB(void)
{
    __asm__ volatile ("isb 0xF" ::: "memory");
}

// ============================================================================
// CMSIS Compatibility Macros
// ============================================================================

#define NVIC_SetPriority         __NVIC_SetPriority
#define NVIC_GetPriority         __NVIC_GetPriority
#define NVIC_EnableIRQ           __NVIC_EnableIRQ
#define NVIC_DisableIRQ          __NVIC_DisableIRQ
#define NVIC_ClearPendingIRQ     __NVIC_ClearPendingIRQ

// ============================================================================
// NVIC Functions
// ============================================================================

// Set Interrupt Priority
static inline void __NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority)
{
    if ((int32_t)IRQn >= 0) {
        NVIC->IP[((uint32_t)IRQn)] = (uint8_t)(priority << (8U - __NVIC_PRIO_BITS));
    }
}

// Get Interrupt Priority
static inline uint32_t __NVIC_GetPriority(IRQn_Type IRQn)
{
    if ((int32_t)IRQn >= 0) {
        return ((uint32_t)NVIC->IP[((uint32_t)IRQn)] >> (8U - __NVIC_PRIO_BITS));
    }
    return 0;
}

// Enable Interrupt
static inline void __NVIC_EnableIRQ(IRQn_Type IRQn)
{
    if ((int32_t)IRQn >= 0) {
        NVIC->ISER[(((uint32_t)IRQn) >> 5UL)] = (1UL << (((uint32_t)IRQn) & 0x1FUL));
    }
}

// Disable Interrupt
static inline void __NVIC_DisableIRQ(IRQn_Type IRQn)
{
    if ((int32_t)IRQn >= 0) {
        NVIC->ICER[(((uint32_t)IRQn) >> 5UL)] = (1UL << (((uint32_t)IRQn) & 0x1FUL));
    }
}

// Clear Pending Interrupt
static inline void __NVIC_ClearPendingIRQ(IRQn_Type IRQn)
{
    if ((int32_t)IRQn >= 0) {
        NVIC->ICPR[(((uint32_t)IRQn) >> 5UL)] = (1UL << (((uint32_t)IRQn) & 0x1FUL));
    }
}

#ifdef __cplusplus
}
#endif

#endif // CMSIS_H
