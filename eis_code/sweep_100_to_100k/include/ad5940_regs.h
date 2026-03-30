/**
 * @file ad5940_regs.h
 * @brief AD5940/AD5941 register definitions.
 *
 * Derived from the AD5940/AD5941 Rev.G datasheet.
 * All addresses are 16-bit; data widths are 16-bit or 32-bit as noted.
 */
#ifndef AD5940_REGS_H
#define AD5940_REGS_H

#include <stdint.h>

/* ====================================================================
 * SPI COMMAND BYTES (Table 126)
 * ==================================================================== */
#define SPICMD_SETADDR      0x20
#define SPICMD_READREG      0x6D
#define SPICMD_WRITEREG     0x2D
#define SPICMD_READFIFO     0x5F

/* ====================================================================
 * IDENTIFICATION REGISTERS
 * ==================================================================== */
#define REG_ADIID           0x0400  /* 16-bit, always 0x4144 */
#define REG_CHIPID          0x0404  /* 16-bit */

/* ====================================================================
 * CLOCK REGISTERS
 * ==================================================================== */
#define REG_CLKCON0         0x0408  /* 16-bit */
#define REG_CLKSEL          0x0414  /* 16-bit */
#define REG_CLKEN0          0x0A70  /* 16-bit */
#define REG_CLKEN1          0x0410  /* 16-bit */
#define REG_OSCKEY          0x0A0C  /* 16-bit – key protection */
#define REG_OSCCON          0x0A10  /* 16-bit */
#define REG_HSOSCCON        0x20BC  /* 16-bit */

/* ====================================================================
 * AFE CORE CONFIGURATION REGISTERS
 * ==================================================================== */
#define REG_AFECON          0x2000  /* 32-bit – master AFE enable */
#define REG_SEQCON          0x2004  /* 32-bit – sequencer config */
#define REG_FIFOCON         0x2008  /* 32-bit – FIFO config */
#define REG_SWCON           0x200C  /* 32-bit – switch matrix */
#define REG_HSDACCON        0x2010  /* 32-bit – high speed DAC config */
#define REG_WGCON           0x2014  /* 32-bit – waveform generator */
#define REG_WGDCLEVEL1      0x2018  /* 32-bit – trapezoid DC level 1 */
#define REG_WGDCLEVEL2      0x201C  /* 32-bit – trapezoid DC level 2 */
#define REG_WGFCW           0x2030  /* 32-bit – sine freq control word */
#define REG_WGPHASE         0x2034  /* 32-bit – sine phase offset */
#define REG_WGOFFSET        0x2038  /* 32-bit – sine offset */
#define REG_WGAMPLITUDE     0x203C  /* 32-bit – sine amplitude */
#define REG_ADCFILTERCON    0x2044  /* 32-bit – ADC filter config */
#define REG_HSDACDAT        0x2048  /* 32-bit – DAC code register */
#define REG_LPREFBUFCON     0x2050  /* 32-bit – LP reference buffer */

/* ADC results */
#define REG_ADCDAT          0x2074  /* 32-bit – raw ADC data */
#define REG_DFTREAL         0x2078  /* 32-bit – DFT real result (18-bit signed) */
#define REG_DFTIMAG         0x207C  /* 32-bit – DFT imaginary result (18-bit signed) */
#define REG_SINC2DAT        0x2080  /* 32-bit – sinc2 output */
#define REG_TEMPSENSDAT     0x2084  /* 32-bit – temp sensor */

/* Sequencer */
#define REG_SEQCNT          0x2064  /* 32-bit – seq command count */

/* AFE general interrupt status */
#define REG_AFEGENINTSTA    0x209C  /* 32-bit */

/* DFT */
#define REG_DFTCON          0x20D0  /* 32-bit – DFT configuration */

/* High Speed TIA */
#define REG_HSRTIACON       0x20F0  /* 32-bit – HS RTIA config */
#define REG_DE0RESCON       0x20F8  /* 32-bit – DE0 resistor config */
#define REG_HSTIACON        0x20FC  /* 32-bit – HS TIA config */

/* Low Power DAC */
#define REG_LPDACDAT0       0x2120  /* 32-bit – LP DAC data */
#define REG_LPDACSW0        0x2124  /* 32-bit – LP DAC switch control */
#define REG_LPDACCON0       0x2128  /* 32-bit – LP DAC config */

/* Switch matrix full control */
#define REG_DSWFULLCON      0x2150  /* 32-bit */
#define REG_NSWFULLCON      0x2154  /* 32-bit */
#define REG_PSWFULLCON      0x2158  /* 32-bit */
#define REG_TSWFULLCON      0x215C  /* 32-bit */

/* Switch matrix status (read-only) */
#define REG_DSWSTA          0x21B0  /* 32-bit */
#define REG_PSWSTA          0x21B4  /* 32-bit */
#define REG_NSWSTA          0x21B8  /* 32-bit */
#define REG_TSWSTA          0x21BC  /* 32-bit */

/* ADC configuration */
#define REG_ADCCON          0x21A8  /* 32-bit */
#define REG_REPEATADCCNV    0x21F0  /* 32-bit */

/* Power mode / bandwidth */
#define REG_PMBW            0x22F0  /* 32-bit */

/* Common-mode mux */
#define REG_SWMUX           0x235C  /* 32-bit */

/* Calibration */
#define REG_CALDATLOCK      0x2230  /* 32-bit */
#define REG_ADCOFFSETHSTIA  0x2234  /* 32-bit */
#define REG_ADCGAINHSTIA    0x2284  /* 32-bit */

/* ====================================================================
 * INTERRUPT CONTROLLER REGISTERS
 * ==================================================================== */
#define REG_INTCPOL         0x3000  /* 32-bit – polarity */
#define REG_INTCCLR         0x3004  /* 32-bit – clear */
#define REG_INTCSEL0        0x3008  /* 32-bit – select 0 */
#define REG_INTCSEL1        0x300C  /* 32-bit – select 1 */
#define REG_INTCFLAG0       0x3010  /* 32-bit – flag 0 */
#define REG_INTCFLAG1       0x3014  /* 32-bit – flag 1 */

/* ====================================================================
 * INTERRUPT BIT POSITIONS (Table 141/142)
 * ==================================================================== */
#define INTC_ADCRESULT      BIT(0)
#define INTC_DFTRESULT      BIT(1)
#define INTC_SINC2RESULT    BIT(2)
#define INTC_TEMPRESULT     BIT(3)
#define INTC_ADCMIN         BIT(4)
#define INTC_ADCMAX         BIT(5)
#define INTC_ADCDELTA       BIT(6)
#define INTC_MEAN           BIT(7)
#define INTC_VARIANCE       BIT(8)
#define INTC_CUSTOM0        BIT(9)
#define INTC_CUSTOM1        BIT(10)
#define INTC_CUSTOM2        BIT(11)
#define INTC_CUSTOM3        BIT(12)
#define INTC_BOOTDONE       BIT(13)
#define INTC_ENDSEQ         BIT(15)
#define INTC_SEQTIMEOUT     BIT(16)
#define INTC_SEQTIMEOUTERR  BIT(17)
#define INTC_FIFOFULL       BIT(23)
#define INTC_FIFOEMPTY      BIT(24)
#define INTC_FIFOTHRESH     BIT(25)
#define INTC_FIFOOVERFLOW   BIT(26)
#define INTC_FIFOUNDERFLOW  BIT(27)

/* ====================================================================
 * AFECON REGISTER BIT POSITIONS
 * ==================================================================== */
#define AFECON_DACBUFEN     BIT(21)
#define AFECON_DACREFEN     BIT(20)
#define AFECON_SINC2EN      BIT(16)
#define AFECON_DFTEN        BIT(15)
#define AFECON_WAVEGENEN    BIT(14)
#define AFECON_TEMPCONVEN   BIT(13)
#define AFECON_TEMPSENSEN   BIT(12)
#define AFECON_TIAEN        BIT(11)  /* High-speed TIA */
#define AFECON_INAMPEN      BIT(10)  /* Excitation inamp */
#define AFECON_EXBUFEN      BIT(9)   /* Excitation buffer */
#define AFECON_ADCCONVEN    BIT(8)   /* Start ADC conversion */
#define AFECON_ADCEN        BIT(7)   /* ADC power */
#define AFECON_DACEN        BIT(6)   /* HS DAC enable */
#define AFECON_HSREFDIS     BIT(5)   /* 1 = power down HS ref */
/* Bits [4:0] reserved, bit 19 reserved but must be written as 1 */
#define AFECON_RSVD19       BIT(19)  /* Always write 1 */

/* ====================================================================
 * PMBW REGISTER
 * ==================================================================== */
#define PMBW_SYSHS          BIT(0)   /* 1 = high power mode (>80 kHz) */
#define PMBW_SYSBW_SHIFT    2
#define PMBW_SYSBW_MASK     (0x3 << PMBW_SYSBW_SHIFT)

/* ====================================================================
 * HSDACCON REGISTER
 * ==================================================================== */
#define HSDACCON_INAMPGNMDE BIT(12)  /* 0 = gain 2, 1 = gain 0.25 */
#define HSDACCON_RATE_SHIFT 1
#define HSDACCON_RATE_MASK  (0xFF << HSDACCON_RATE_SHIFT)
#define HSDACCON_ATTENEN    BIT(0)   /* 0 = gain 1, 1 = gain 0.2 */

/* Recommended rate dividers for impedance measurement */
#define HSDACCON_RATE_LP    0x1B     /* Low power: ACLK/0x1B */
#define HSDACCON_RATE_HP    0x07     /* High power: ACLK/0x07 */

/* ====================================================================
 * WGCON REGISTER
 * ==================================================================== */
#define WGCON_DACGAINCAL    BIT(5)
#define WGCON_DACOFFSETCAL  BIT(4)
#define WGCON_TYPESEL_SHIFT 1
#define WGCON_TYPESEL_MASK  (0x3 << WGCON_TYPESEL_SHIFT)
#define WGCON_TYPE_DIRECT   (0x0 << WGCON_TYPESEL_SHIFT)
#define WGCON_TYPE_SINE     (0x2 << WGCON_TYPESEL_SHIFT)
#define WGCON_TYPE_TRAPEZ   (0x3 << WGCON_TYPESEL_SHIFT)

/* ====================================================================
 * DFTCON REGISTER
 * ==================================================================== */
#define DFTCON_HANNINGEN    BIT(0)
#define DFTCON_DFTNUM_SHIFT 4
#define DFTCON_DFTNUM_MASK  (0xF << DFTCON_DFTNUM_SHIFT)
#define DFTCON_DFTINSEL_SHIFT 20
#define DFTCON_DFTINSEL_MASK  (0x3 << DFTCON_DFTINSEL_SHIFT)

/* DFT point number options (field value for DFTNUM[7:4]) */
#define DFTNUM_4            0
#define DFTNUM_8            1
#define DFTNUM_16           2
#define DFTNUM_32           3
#define DFTNUM_64           4
#define DFTNUM_128          5
#define DFTNUM_256          6
#define DFTNUM_512          7
#define DFTNUM_1024         8
#define DFTNUM_2048         9
#define DFTNUM_4096         10
#define DFTNUM_8192         11
#define DFTNUM_16384        12

/* DFT input select (field value for DFTINSEL[21:20]) */
#define DFTINSEL_SINC2      0
#define DFTINSEL_SINC3      1    /* After gain/offset correction */
#define DFTINSEL_ADCRAW     2    /* ADC raw, 800 kHz only */

/* ====================================================================
 * ADCFILTERCON REGISTER
 * ==================================================================== */
#define ADCFILTERCON_ADCSAMPLERATE BIT(0)  /* 1 = 800 kHz, 0 = 1.6 MHz */
#define ADCFILTERCON_LPFBYPEN    BIT(4)    /* 1 = bypass notch filter */
#define ADCFILTERCON_SINC3BYP    BIT(6)    /* 1 = bypass sinc3 */
#define ADCFILTERCON_AVRGEN      BIT(7)    /* 1 = enable averaging */
#define ADCFILTERCON_SINC2OSR_SHIFT 8
#define ADCFILTERCON_SINC2OSR_MASK  (0xF << ADCFILTERCON_SINC2OSR_SHIFT)
#define ADCFILTERCON_SINC3OSR_SHIFT 12
#define ADCFILTERCON_SINC3OSR_MASK  (0x3 << ADCFILTERCON_SINC3OSR_SHIFT)
#define ADCFILTERCON_AVRG_SHIFT     14
#define ADCFILTERCON_AVRG_MASK      (0x3 << ADCFILTERCON_AVRG_SHIFT)

/* Sinc3 OSR options */
#define SINC3OSR_5          0    /* 800 kHz / 5 = 160 kHz output */
#define SINC3OSR_4          1    /* 1.6 MHz / 4 = 400 kHz (high pwr) */
#define SINC3OSR_2          2    /* 800 kHz / 2 = 400 kHz */

/* ====================================================================
 * ADCCON REGISTER
 * ==================================================================== */
#define ADCCON_GNPGA_SHIFT      16
#define ADCCON_GNPGA_MASK       (0x7 << ADCCON_GNPGA_SHIFT)
#define ADCCON_GNOFSELPGA       BIT(15)
#define ADCCON_MUXSELN_SHIFT    8
#define ADCCON_MUXSELN_MASK     (0x1F << ADCCON_MUXSELN_SHIFT)
#define ADCCON_MUXSELP_SHIFT    0
#define ADCCON_MUXSELP_MASK     (0x3F)

/* ADC positive input MUX selections */
#define ADCMUXP_FLOAT       0x00
#define ADCMUXP_HSTIA_P     0x01  /* High speed TIA positive */
#define ADCMUXP_AIN0        0x04
#define ADCMUXP_AIN1        0x05
#define ADCMUXP_AIN2        0x06
#define ADCMUXP_AIN3        0x07
#define ADCMUXP_AVDD_2      0x08
#define ADCMUXP_DVDD_2      0x09
#define ADCMUXP_DE0         0x0D
#define ADCMUXP_SE0         0x0E
#define ADCMUXP_VREF1V25    0x10
#define ADCMUXP_VREF1V82    0x12
#define ADCMUXP_TEMPSENS_N  0x13
#define ADCMUXP_AIN4        0x14
#define ADCMUXP_AIN6        0x16
#define ADCMUXP_VZERO0      0x17
#define ADCMUXP_VBIAS0      0x18
#define ADCMUXP_VCE0        0x19
#define ADCMUXP_VRE0        0x1A
#define ADCMUXP_VCE0_2      0x1E
#define ADCMUXP_LPTIA_P     0x20
#define ADCMUXP_AGND        0x22
#define ADCMUXP_EXCIT_P     0x24

/* ADC negative input MUX selections */
#define ADCMUXN_FLOAT       0x00
#define ADCMUXN_HSTIA_N     0x01  /* High speed TIA negative */
#define ADCMUXN_LPTIA_N     0x02
#define ADCMUXN_AIN0        0x04
#define ADCMUXN_AIN1        0x05
#define ADCMUXN_AIN2        0x06
#define ADCMUXN_AIN3        0x07
#define ADCMUXN_VBIAS_CAP   0x08
#define ADCMUXN_AIN4        0x0C
#define ADCMUXN_AIN6        0x0E
#define ADCMUXN_VZERO0      0x10
#define ADCMUXN_VBIAS0      0x11
#define ADCMUXN_EXCIT_N     0x14

/* PGA gain options */
#define ADCPGA_1            0
#define ADCPGA_1P5          1
#define ADCPGA_2            2
#define ADCPGA_4            3
#define ADCPGA_9            4

/* ====================================================================
 * HSRTIACON REGISTER
 * ==================================================================== */
#define HSRTIACON_RTIACON_SHIFT   0
#define HSRTIACON_RTIACON_MASK    0x0F
#define HSRTIACON_TIASW6CON       BIT(4)  /* Diode parallel with RTIA */
#define HSRTIACON_CTIACON_SHIFT   5
#define HSRTIACON_CTIACON_MASK    (0xFF << HSRTIACON_CTIACON_SHIFT)

/* RTIA resistance values */
#define HSRTIA_200          0x0
#define HSRTIA_1K           0x1
#define HSRTIA_5K           0x2
#define HSRTIA_10K          0x3
#define HSRTIA_20K          0x4
#define HSRTIA_40K          0x5
#define HSRTIA_80K          0x6
#define HSRTIA_160K         0x7
#define HSRTIA_OPEN         0xF

/* Lookup table for RTIA values in ohms */
static const uint32_t hsrtia_ohms[] = {
    200, 1000, 5000, 10000, 20000, 40000, 80000, 160000
};

/* ====================================================================
 * HSTIACON REGISTER
 * ==================================================================== */
#define HSTIACON_VBIASSEL_SHIFT   0
#define HSTIACON_VBIASSEL_MASK    0x03
#define HSTIA_VBIAS_CAP     0   /* Internal 1.11 V */
#define HSTIA_VZERO0        1   /* VZERO0 from LP DAC */

/* ====================================================================
 * LPDACCON0 REGISTER
 * ==================================================================== */
#define LPDACCON0_RSTEN     BIT(0)   /* Enable writes to LPDACDAT0 */
#define LPDACCON0_PWDEN     BIT(1)   /* 1 = powered down */
#define LPDACCON0_REFSEL    BIT(2)   /* 0 = 2.5 V LP ref, 1 = AVDD */
#define LPDACCON0_VBIASMUX  BIT(3)   /* 0 = 12-bit to VBIAS0, 1 = 6-bit */
#define LPDACCON0_VZEROMUX  BIT(4)   /* 0 = 6-bit to VZERO0, 1 = 12-bit */
#define LPDACCON0_DACMDE    BIT(5)   /* 0 = normal, 1 = diagnostic */

/* ====================================================================
 * LPDACSW0 REGISTER
 * ==================================================================== */
#define LPDACSW0_SW0        BIT(0)   /* VZERO0 -> HSTIA positive */
#define LPDACSW0_SW1        BIT(1)   /* VZERO0 -> VZERO0 pin */
#define LPDACSW0_SW2        BIT(2)   /* VZERO0 -> LPTIA positive */
#define LPDACSW0_SW3        BIT(3)   /* VBIAS0 -> VBIAS0 pin */
#define LPDACSW0_SW4        BIT(4)   /* VBIAS0 -> potentiostat */
#define LPDACSW0_LPMODEDIS  BIT(5)   /* 1 = individual SW control */

/* ====================================================================
 * SWCON REGISTER – GROUPED SWITCH CONTROL
 * ==================================================================== */
#define SWCON_DMUXCON_SHIFT     0
#define SWCON_DMUXCON_MASK      (0xF)
#define SWCON_PMUXCON_SHIFT     4
#define SWCON_PMUXCON_MASK      (0xF << 4)
#define SWCON_NMUXCON_SHIFT     8
#define SWCON_NMUXCON_MASK      (0xF << 8)
#define SWCON_TMUXCON_SHIFT     12
#define SWCON_TMUXCON_MASK      (0xF << 12)
#define SWCON_SWSOURCESEL       BIT(16) /* 1 = use xSWFULLCON regs */
#define SWCON_T9CON             BIT(17) /* 1 = T9 closed */
#define SWCON_T10CON            BIT(18) /* 1 = T10 closed */

/* Grouped switch values (common named positions) */
/* D switches: excitation amplifier output */
#define DSWCON_ALLOPEN      0x0
#define DSWCON_DR0          0x1  /* -> RCAL0 */
#define DSWCON_D2           0x2
#define DSWCON_D3           0x3
#define DSWCON_D4           0x4  /* -> CE0 on many configs */
#define DSWCON_D5           0x5
#define DSWCON_D6           0x6
#define DSWCON_D7           0x7  /* -> DE0 */
#define DSWCON_D8           0x8

/* P switches: excitation amplifier positive input (feedback) */
#define PSWCON_PL           0x0
#define PSWCON_PR0          0x1  /* -> RCAL0 */
#define PSWCON_P2           0x2  /* -> RE0 on many configs */
#define PSWCON_P3           0x3
#define PSWCON_P4           0x4
#define PSWCON_P5           0x5
#define PSWCON_P6           0x6
#define PSWCON_P7           0x7
#define PSWCON_P8           0x8
#define PSWCON_P9           0x9
#define PSWCON_P11          0xB
#define PSWCON_PL2          0xD
#define PSWCON_ALLOPEN      0xF

/* N switches: HSTIA N input */
#define NSWCON_NL           0x0
#define NSWCON_N1           0x1  /* -> SE0 */
#define NSWCON_N2           0x2
#define NSWCON_N3           0x3
#define NSWCON_N4           0x4
#define NSWCON_N5           0x5
#define NSWCON_N6           0x6
#define NSWCON_N7           0x7
#define NSWCON_N9           0x9
#define NSWCON_NR1          0xA  /* -> RCAL1 */
#define NSWCON_NL2          0xB
#define NSWCON_ALLOPEN      0xF

/* T switches: HSTIA inverting input */
#define TSWCON_ALLOPEN      0x0
#define TSWCON_T1           0x1  /* -> SE0 */
#define TSWCON_T2           0x2
#define TSWCON_T3           0x3
#define TSWCON_T4           0x4
#define TSWCON_T5           0x5
#define TSWCON_T6           0x6
#define TSWCON_T7           0x7
#define TSWCON_TR1          0x8  /* -> RCAL1 */
#define TSWCON_ALLCLOSED    0x9

/* ====================================================================
 * FIFOCON REGISTER
 * ==================================================================== */
#define FIFOCON_DATAFIFOEN      BIT(11)
#define FIFOCON_SRCSEL_SHIFT    13
#define FIFOCON_SRCSEL_MASK     (0x7 << FIFOCON_SRCSEL_SHIFT)
#define FIFOCON_SRC_ADC         (0x0 << FIFOCON_SRCSEL_SHIFT)
#define FIFOCON_SRC_DFT         (0x2 << FIFOCON_SRCSEL_SHIFT)
#define FIFOCON_SRC_SINC2       (0x3 << FIFOCON_SRCSEL_SHIFT)
#define FIFOCON_SRC_VAR         (0x4 << FIFOCON_SRCSEL_SHIFT)
#define FIFOCON_SRC_MEAN        (0x5 << FIFOCON_SRCSEL_SHIFT)

/* ====================================================================
 * SEQCON REGISTER
 * ==================================================================== */
#define SEQCON_SEQEN            BIT(0)
#define SEQCON_HALTFIFOEMPTY    BIT(1)
#define SEQCON_SEQHALT          BIT(4)

/* ====================================================================
 * DATA FIFO READ REGISTER
 * ==================================================================== */
#define REG_DATAFIFORD      0x2084  /* 32-bit – read FIFO data */

/* ====================================================================
 * LOW POWER REFERENCE
 * ==================================================================== */
#define LPREFBUFCON_LPREFDIS    BIT(0)
#define LPREFBUFCON_LPBUF2P5DIS BIT(1)

#endif /* AD5940_REGS_H */
