/*
 *
 * This code is a reproduction of standard TI code
 * Copyright (c) 2020, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Texas Instruments Incorporated nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <lab6_helper.h>

void delay_cycles(uint32_t cycles)
{
    /* this is a scratch register for the compiler to use */
    uint32_t scratch;
    /* There will be a 2 cycle delay here to fetch & decode instructions
     * if branch and linking to this function */
    /* Subtract 2 net cycles for constant offset: +2 cycles for entry jump,
     * +2 cycles for exit, -1 cycle for a shorter loop cycle on the last loop,
     * -1 for this instruction */
    __asm volatile(
#ifdef __GNUC__
        ".syntax unified\n\t"
#endif
        "SUBS %0, %[numCycles], #2; \n"
        "%=: \n\t"
        "SUBS %0, %0, #4; \n\t"
        "NOP; \n\t"
        "BHS %=b;" /* branches back to the label defined above if number > 0 */
        /* Return: 2 cycles */
        : "=&r"(scratch)
        : [ numCycles ] "r"(cycles));
}

void InitializeProcessor(void) {
    SYSCTL->SOCLOCK.BORTHRESHOLD = SYSCTL_SYSSTATUS_BORCURTHRESHOLD_BORMIN; // Brownout generates a reset.
    update_reg(&SYSCTL->SOCLOCK.MCLKCFG, (uint32_t) SYSCTL_MCLKCFG_UDIV_NODIVIDE,
        SYSCTL_MCLKCFG_UDIV_MASK); // Set UPCLK divider
    update_reg(&SYSCTL->SOCLOCK.SYSOSCCFG, SYSCTL_SYSOSCCFG_FREQ_SYSOSCBASE,
        SYSCTL_SYSOSCCFG_FREQ_MASK); // Set SYSOSC to 32 MHz
    // Disable MCLK Divider
    update_reg(&SYSCTL->SOCLOCK.MCLKCFG, (uint32_t) 0x0, SYSCTL_MCLKCFG_MDIV_MASK);
}

void InitializeGPIO(void) {
    GPIOA->GPRCM.RSTCTL = (GPIO_RSTCTL_KEY_UNLOCK_W | GPIO_RSTCTL_RESETSTKYCLR_CLR
        | GPIO_RSTCTL_RESETASSERT_ASSERT);
    GPIOA->GPRCM.PWREN = (GPIO_PWREN_KEY_UNLOCK_W | GPIO_PWREN_ENABLE_ENABLE);
    delay_cycles(POWER_STARTUP_DELAY); // delay to enable GPIO to turn on and reset
    
    // Initialize SPI0 connections
    IOMUX->SECCFG.PINCM[(IOMUX_PINCM22)] = IOMUX_PINCM_PC_CONNECTED |
        IOMUX_PINCM22_PF_SPI0_SCLK; // SPI0_SCLK on PA11
    IOMUX->SECCFG.PINCM[(IOMUX_PINCM20)] = IOMUX_PINCM_PC_CONNECTED |
        IOMUX_PINCM20_PF_SPI0_PICO; // SPI0_PCIO on PA9
    
    // Initialize PWM connection
    IOMUX->SECCFG.PINCM[(IOMUX_PINCM37)] = IOMUX_PINCM_PC_CONNECTED |
        IOMUX_PINCM37_PF_TIMA1_CCP0; // TIMA1-CC0 on PA15
    
    // Initialize GPIO input connections
    uint32_t input_configuration = IOMUX_PINCM_PC_CONNECTED |
        IOMUX_PINCM_INENA_ENABLE |
        ((uint32_t) 0x00000001) | // GPIO function is always MUX entry 1
        IOMUX_PINCM_INV_DISABLE | // TODO: experiment with setting this to invert our logic
        IOMUX_PINCM_PIPU_ENABLE | IOMUX_PINCM_PIPD_DISABLE | // pull up resistor - switch connects to ground
        IOMUX_PINCM_HYSTEN_DISABLE | // disable input pin hysteresis (TODO: experiment with this)
        IOMUX_PINCM_WUEN_DISABLE; // wake-up disable (TODO: experiment with this for ultra low power!)
    
    IOMUX->SECCFG.PINCM[(IOMUX_PINCM53)] = input_configuration; // PA23
    IOMUX->SECCFG.PINCM[(IOMUX_PINCM54)] = input_configuration; // PA24
    IOMUX->SECCFG.PINCM[(IOMUX_PINCM55)] = input_configuration; // PA25
    IOMUX->SECCFG.PINCM[(IOMUX_PINCM59)] = input_configuration; // PA26
    
    delay_cycles(POWER_STARTUP_DELAY); // delay to enable GPIO to turn on and reset
}

void InitializeSPI(void) {
    SPI0->GPRCM.RSTCTL = (SPI_RSTCTL_KEY_UNLOCK_W | SPI_RSTCTL_RESETSTKYCLR_CLR |
        SPI_RSTCTL_RESETASSERT_ASSERT);
    SPI0->GPRCM.PWREN = (SPI_PWREN_KEY_UNLOCK_W | SPI_PWREN_ENABLE_ENABLE);
    delay_cycles(POWER_STARTUP_DELAY); // delay to enable SPI to turn on and reset
    
    // Configure clocking for SPI0
    SPI0->CLKSEL = (uint32_t) SPI_CLKSEL_SYSCLK_SEL_ENABLE; // use the SYSOSC
    SPI0->CLKDIV = (uint32_t) SPI_CLKDIV_RATIO_DIV_BY_1; // actually 0x0, which is going to be default, but here for completeness
    
    // Configure the module
    SPI0->CTL0 = SPI_CTL0_SPO_HIGH | SPI_CTL0_SPH_SECOND | // Clock edges and phases for data
        SPI_CTL0_FRF_MOTOROLA_3WIRE | // Don't use a chip select pin to bound frames
        SPI_CTL0_DSS_DSS_16;
    
    SPI0->CTL1 = SPI_CTL1_CP_ENABLE | // Microcontroller is CONTROLLER
        SPI_CTL1_PREN_DISABLE | SPI_CTL1_PTEN_DISABLE | // Disable parity on RX and TX
        SPI_CTL1_MSB_ENABLE; // Bit order is MSB first
    
    /* Configure Controller mode */
    /*
     * Set the bit rate clock divider to generate the serial output clock
     * outputBitRate = (spiInputClock) / ((1 + SCR) * 2)
     * 500000 = (32000000)/((1 + 31) * 2)
     */
    SPI0->CLKCTL = 31;
    
    /* Set RX and TX FIFO threshold levels */
    SPI0->IFLS = SPI_IFLS_RXIFLSEL_LEVEL_1 | // Make the FIFO just one word big
        SPI_IFLS_TXIFLSEL_LVL_EMPTY; // Trigger an interrupt when the FIFO is empty
    
    /* Enable Transmit FIFO interrupt */
    SPI0->CPU_INT.IMASK |= SPI_CPU_INT_IMASK_TX_SET;
    
    /* Enable module */
    SPI0->CTL1 |= SPI_CTL1_ENABLE_ENABLE;
}

void InitializeTimerG0(void) {
    TIMG0->GPRCM.RSTCTL = (GPIO_RSTCTL_KEY_UNLOCK_W | GPIO_RSTCTL_RESETSTKYCLR_CLR
        | GPIO_RSTCTL_RESETASSERT_ASSERT);
    TIMG0->GPRCM.PWREN = (GPIO_PWREN_KEY_UNLOCK_W | GPIO_PWREN_ENABLE_ENABLE);
    delay_cycles(POWER_STARTUP_DELAY); // delay to enable GPIO to turn on and reset
    
    // Configure clocking for Timer Module
    TIMG0->CLKSEL = GPTIMER_CLKSEL_LFCLK_SEL_ENABLE;
    TIMG0->CLKDIV = GPTIMER_CLKDIV_RATIO_DIV_BY_1;
    
    /* This will configure what happens when we count down to zero - we'll set counter to LOAD value */
    TIMG0->COUNTERREGS.CC_01[0] = 0; // Count down to zero (value in CC0), then reload
    TIMG0->COUNTERREGS.CCCTL_01[0] = GPTIMER_CCCTL_01_ACOND_TIMCLK; // Set timer to advance on TIMCLK ticks
    
    // CM_DOWN - count down mode; REPEAT_1 - keep going when we reach zero;
    // CVAE_LDVAL - when we get to zero, reload LOAD value;
    // EN_DISABLED - Start with timer disabled
    TIMG0->COUNTERREGS.CTRCTL =
        (( GPTIMER_CTRCTL_CVAE_LDVAL | GPTIMER_CTRCTL_CM_DOWN |
        GPTIMER_CTRCTL_REPEAT_REPEAT_1) | GPTIMER_CTRCTL_EN_DISABLED);
    
    // Enable timer interrupt when we reach 0
    TIMG0->CPU_INT.IMASK |= GPTIMER_CPU_INT_IMASK_Z_SET;
    
    TIMG0->PDBGCTL = GPTIMER_PDBGCTL_SOFT_IMMEDIATE;
    TIMG0->COMMONREGS.CCLKCTL = (GPTIMER_CCLKCTL_CLKEN_ENABLED);
}

void InitializeTimerA1_PWM(void) {
    TIMA1->GPRCM.RSTCTL = (GPIO_RSTCTL_KEY_UNLOCK_W | GPIO_RSTCTL_RESETSTKYCLR_CLR
        | GPIO_RSTCTL_RESETASSERT_ASSERT);
    TIMA1->GPRCM.PWREN = (GPIO_PWREN_KEY_UNLOCK_W | GPIO_PWREN_ENABLE_ENABLE);
    delay_cycles(POWER_STARTUP_DELAY); // delay to enable GPIO to turn on and reset
    
    // Configure clocking for Timer Module
    TIMA1->CLKSEL = GPTIMER_CLKSEL_BUSCLK_SEL_ENABLE; // BUSCLK will be SYSOSC / 32 kHz
    TIMA1->CLKDIV = GPTIMER_CLKDIV_RATIO_DIV_BY_4; // Divide by 4 to make PWM clock frequency 8 MHz
    
    TIMA1->COUNTERREGS.CCACT_01[0] = GPTIMER_CCACT_01_ZACT_CCP_HIGH |
        GPTIMER_CCACT_01_CUACT_CCP_LOW;
    TIMA1->COUNTERREGS.CCACT_01[1] = GPTIMER_CCACT_01_ZACT_CCP_HIGH |
        GPTIMER_CCACT_01_CUACT_CCP_LOW;
    
    TIMA1->COUNTERREGS.CTRCTL = GPTIMER_CTRCTL_REPEAT_REPEAT_1 |
        GPTIMER_CTRCTL_CM_UP |
        GPTIMER_CTRCTL_CVAE_ZEROVAL | GPTIMER_CTRCTL_EN_DISABLED;
    
    TIMA1->COMMONREGS.CCPD = GPTIMER_CCPD_C0CCP0_OUTPUT |
        GPTIMER_CCPD_C0CCP1_OUTPUT;
    
    TIMA1->COMMONREGS.CCLKCTL = (GPTIMER_CCLKCTL_CLKEN_ENABLED);
    
    TIMA1->COUNTERREGS.LOAD = 3999; // Period is LOAD+1 - this is 8000000/4000 = 2kHz
    // HEADS UP: This sets the frequency of the buzzer!
    TIMA1->COUNTERREGS.CC_01[0] = (TIMA1->COUNTERREGS.LOAD + 1) / 2; // half of load to make this a square wave
    
    TIMA1->COUNTERREGS.CTRCTL |= (GPTIMER_CTRCTL_EN_ENABLED);
}