/*
 * DC34 DEF CON Badge Fuzzer SAO  -- firmware
 * MCU: ATtiny1614-SSN  (UPDI on PA0, SerialUPDI programmer)
 * Tool: avr-gcc + avr-libc, -mmcu=attiny1614
 *
 * Modes (selected by SW1 multi-press inside a 350 ms window):
 *   1x SW1 -> Mode 1 : GPIO1/2 stimulus + response sampling   (left port)
 *   2x SW1 -> Mode 2 : GPIO4 open-drain wake assert           (right port)
 *   3x SW1 -> Mode 3 : I2C slave-listen sweep 0x08..0x77
 *                      (skips 0x19 accel and 0x3C OLED)
 *
 * SW2 fires the currently armed attack.
 * Optional 9600 8N1 debug log on PA1 (USART0 TXD).
 */

#define F_CPU 10000000UL         /* 20 MHz osc / 2, set at boot (see clock_init) */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>

/* --- PORTA pin masks --------------------------------------------------- */
#define PIN_TXD   PIN1_bm        /* PA1  USART0 TXD (optional debug)      */
#define PIN_SDA   PIN2_bm        /* PA2  TWI SDA  -> SAO pin 3            */
#define PIN_SCL   PIN3_bm        /* PA3  TWI SCL  -> SAO pin 4            */
#define PIN_SW2   PIN4_bm        /* PA4  attack trigger, active-LOW       */
#define PIN_ACT   PIN5_bm        /* PA5  green LED                        */
#define PIN_HIT   PIN6_bm        /* PA6  amber LED                        */
#define PIN_ERR   PIN7_bm        /* PA7  red   LED                        */

/* --- PORTB pin masks --------------------------------------------------- */
#define PIN_MODE  PIN0_bm        /* PB0  blue  LED                        */
#define PIN_SW1   PIN1_bm        /* PB1  mode-cycle, active-LOW           */
#define PIN_GPIO2 PIN2_bm        /* PB2  GPIO2 / GPIO4-OD  -> SAO pin 6   */
#define PIN_GPIO1 PIN3_bm        /* PB3  GPIO1 / GPIO3     -> SAO pin 5   */

/* --- Timing ------------------------------------------------------------ */
#define BLINK_MS        100u
#define MODE_WINDOW_MS  350u     /* multi-press detection window          */
#define M3_LISTEN_MS    75u      /* listen time per slave address         */

/* --- USART0 baud register for 9600 baud:
 * BAUD = (F_CPU * 64) / (16 * 9600)  = F_CPU * 4 / 9600.
 * At F_CPU = 10 MHz: 4166.67 -> use 4167.                                  */
#define UART_BAUD_REG   4167u

/* ====================================================================== */
/*  Globals                                                                */
/* ====================================================================== */
static volatile uint8_t g_mode = 1;

/* ====================================================================== */
/*  LED helpers                                                            */
/* ====================================================================== */
static inline void leds_off(void)
{
    PORTA.OUTCLR = PIN_ACT | PIN_HIT | PIN_ERR;
    PORTB.OUTCLR = PIN_MODE;
}

static void blink_n(volatile PORT_t *port, uint8_t pin, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        port->OUTSET = pin;
        _delay_ms(BLINK_MS);
        port->OUTCLR = pin;
        _delay_ms(BLINK_MS);
    }
}

/* ====================================================================== */
/*  Debug UART  (PA1 / USART0 TXD)                                         */
/* ====================================================================== */
static void uart_init(void)
{
    PORTA.DIRSET = PIN_TXD;
    USART0.BAUD  = UART_BAUD_REG;
    USART0.CTRLB = USART_TXEN_bm;
}

static void uart_putc(char c)
{
    while (!(USART0.STATUS & USART_DREIF_bm))
        ;
    USART0.TXDATAL = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void uart_hex8(uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[v >> 4]);
    uart_putc(hex[v & 0x0F]);
}

/* ====================================================================== */
/*  Mode confirmation (called after SW1 selects a mode)                    */
/* ====================================================================== */
static void confirm_mode(uint8_t mode)
{
    leds_off();
    switch (mode) {
    case 1: blink_n(&PORTA, PIN_ACT, 1); break;
    case 2: blink_n(&PORTA, PIN_HIT, 2); break;
    case 3: blink_n(&PORTA, PIN_ERR, 3); break;
    default: break;
    }
    PORTB.OUTSET = PIN_MODE;     /* MODE steady once confirmed */

    uart_puts("MODE ");
    uart_putc('0' + mode);
    uart_puts("\r\n");
}

/* ====================================================================== */
/*  Mode 1 : GPIO1/2 stimulus + response sampling                          */
/* ====================================================================== */
/*
 * The badge has pull-ups (2.2 k .. 47 k) on GPIO1/GPIO2.  Baseline with
 * the SAO pins tristated should read HIGH on both lines.  We:
 *   1. drive each 2-bit pattern (00,01,10,11) briefly,
 *   2. release to Hi-Z and sample,
 *   3. log via UART if the sample deviates from the pull-up baseline,
 *   4. sit Hi-Z for ~1.5 s afterwards looking for badge-initiated edges.
 */
static void attack_mode1(void)
{
    PORTA.OUTSET = PIN_ACT;

    /* Both lines tristated -> read baseline */
    PORTB.DIRCLR = PIN_GPIO1 | PIN_GPIO2;
    PORTB.OUTCLR = PIN_GPIO1 | PIN_GPIO2;
    _delay_ms(2);
    uint8_t baseline = PORTB.IN & (PIN_GPIO1 | PIN_GPIO2);

    uart_puts("\r\n[M1] base=");
    uart_hex8(baseline);
    uart_puts("\r\n");

    /* 4 stimulus patterns x 4 repeats */
    static const uint8_t pat[4] = {
        0,
        PIN_GPIO1,
        PIN_GPIO2,
        PIN_GPIO1 | PIN_GPIO2,
    };

    for (uint8_t rep = 0; rep < 4; rep++) {
        for (uint8_t i = 0; i < 4; i++) {
            const uint8_t both = PIN_GPIO1 | PIN_GPIO2;

            /* Drive pattern */
            PORTB.OUT    = (PORTB.OUT & ~both) | (pat[i] & both);
            PORTB.DIRSET = both;
            _delay_ms(8);

            /* Release and settle */
            PORTB.DIRCLR = both;
            PORTB.OUTCLR = both;
            _delay_us(150);

            /* Sample */
            uint8_t s = PORTB.IN & both;
            if (s != baseline) {
                PORTA.OUTSET = PIN_HIT;
                uart_puts("[M1] resp pat=");
                uart_hex8(pat[i]);
                uart_puts(" s=");
                uart_hex8(s);
                uart_puts("\r\n");
                _delay_ms(40);
                PORTA.OUTCLR = PIN_HIT;
            }
            _delay_ms(15);
        }
    }

    /* Passive listen for ~1.5 s */
    uart_puts("[M1] listen\r\n");
    PORTB.DIRCLR = PIN_GPIO1 | PIN_GPIO2;
    uint8_t prev = PORTB.IN & (PIN_GPIO1 | PIN_GPIO2);
    for (uint16_t t = 0; t < 1500; t++) {
        uint8_t now = PORTB.IN & (PIN_GPIO1 | PIN_GPIO2);
        if (now != prev) {
            PORTA.OUTSET = PIN_HIT;
            uart_puts("[M1] edge ");
            uart_hex8(now);
            uart_puts("\r\n");
            _delay_ms(25);
            PORTA.OUTCLR = PIN_HIT;
            prev = now;
        }
        _delay_ms(1);
    }

    PORTA.OUTCLR = PIN_ACT;
    uart_puts("[M1] done\r\n");
}

/* ====================================================================== */
/*  Mode 2 : GPIO4 open-drain wake assert  (held while SW2 down)           */
/* ====================================================================== */
static void attack_mode2(void)
{
    uart_puts("\r\n[M2] WAKE assert\r\n");

    /* Drive PB2 LOW (open-drain assert).  Output bit pre-set to 0 so
     * enabling DIR drives the line low immediately. */
    PORTB.OUTCLR = PIN_GPIO2;
    PORTB.DIRSET = PIN_GPIO2;
    PORTA.OUTSET = PIN_ERR;      /* ERR solid while asserted */

    while (!(PORTA.IN & PIN_SW2))
        _delay_ms(1);

    /* Release -> tri-state */
    PORTB.DIRCLR = PIN_GPIO2;
    PORTA.OUTCLR = PIN_ERR;

    uart_puts("[M2] released\r\n");
}

/* ====================================================================== */
/*  Mode 3 : bit-banged I2C slave-listen sweep                             */
/* ====================================================================== */
/*
 * The ATtiny1614's hardware TWI0 peripheral is physically wired to
 * PB0 (SCL) / PB1 (SDA) only -- there is no PORTMUX route that lands
 * SCL on PA3, and tinyAVR does not implement TWI dual-mode.  Since the
 * SAO carries SDA on PA2 and SCL on PA3, we must snoop the bus in
 * software using pin-change interrupts.
 *
 * We never drive either line -- purely passive.  Because we don't ACK,
 * the master sees a NACK for our probed address, which is fine for
 * detection: we've already latched the address byte on SCL rising
 * edges by the time the ACK bit rolls around.
 *
 * Timing budget @ F_CPU = 10 MHz:
 *   ISR entry+body+exit ~ 5 us; 100 kHz SCL half-period is 5 us.
 *   Reliable up to 100 kHz standard-mode I2C.  400 kHz fast-mode may
 *   drop bits.  Most badges run at 100 kHz.
 *
 * Skip list: 0x19 (accel) and 0x3C (OLED) are real silicon on the
 * badge -- listening on those addresses would cause bus contention.
 */

#define BB_STATE_IDLE   0
#define BB_STATE_ADDR   1
#define BB_STATE_ACK    2

static volatile uint8_t  bb_state;
static volatile uint8_t  bb_shift;
static volatile uint8_t  bb_bits;
static volatile uint8_t  bb_target;
static volatile bool     bb_hit;

ISR(PORTA_PORT_vect)
{
    uint8_t flags = PORTA.INTFLAGS;
    uint8_t inp   = PORTA.IN;
    uint8_t scl   = inp & PIN_SCL;   /* PA3 */
    uint8_t sda   = inp & PIN_SDA;   /* PA2 */

    /* SDA edge while SCL high => START (falling) or STOP (rising) */
    if (flags & PIN_SDA) {
        if (scl) {
            if (!sda) {
                bb_state = BB_STATE_ADDR;
                bb_bits  = 0;
                bb_shift = 0;
            } else {
                bb_state = BB_STATE_IDLE;
            }
        }
    }

    /* SCL rising edge => sample SDA, shift into address register */
    if (flags & PIN_SCL) {
        if (bb_state == BB_STATE_ADDR) {
            bb_shift <<= 1;
            if (sda)
                bb_shift |= 1u;
            if (++bb_bits == 8) {
                if ((uint8_t)(bb_shift >> 1) == bb_target)
                    bb_hit = true;
                bb_state = BB_STATE_ACK;
            }
        }
    }

    PORTA.INTFLAGS = flags;      /* clear handled edges */
}

static bool bb_listen_as(uint8_t addr, uint16_t listen_ms)
{
    /* Both lines as passive inputs; badge bus pull-ups hold them high. */
    PORTA.DIRCLR = PIN_SDA | PIN_SCL;

    bb_target = addr;
    bb_hit    = false;
    bb_state  = BB_STATE_IDLE;
    bb_bits   = 0;
    bb_shift  = 0;

    PORTA.INTFLAGS = PIN_SDA | PIN_SCL;         /* clear stale flags */

    PORTA.PIN2CTRL = PORT_ISC_BOTHEDGES_gc;     /* SDA both edges     */
    PORTA.PIN3CTRL = PORT_ISC_RISING_gc;        /* SCL rising only    */

    sei();
    for (uint16_t t = 0; t < listen_ms && !bb_hit; t++)
        _delay_ms(1);
    cli();

    PORTA.PIN2CTRL = 0;
    PORTA.PIN3CTRL = 0;
    PORTA.INTFLAGS = PIN_SDA | PIN_SCL;

    return bb_hit;
}

static void attack_mode3(void)
{
    uart_puts("\r\n[M3] I2C bit-bang listen sweep\r\n");
    uint8_t hits = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (addr == 0x19 || addr == 0x3C)
            continue;            /* skip known badge devices */

        /* Rapid blink: ACT + MODE while sweeping */
        PORTA.OUTTGL = PIN_ACT;
        PORTB.OUTTGL = PIN_MODE;

        if (bb_listen_as(addr, M3_LISTEN_MS)) {
            PORTA.OUTSET = PIN_HIT;
            uart_puts("[M3] HIT 0x");
            uart_hex8(addr);
            uart_puts("\r\n");
            hits++;
            _delay_ms(150);
            PORTA.OUTCLR = PIN_HIT;
        }
    }

    PORTA.OUTCLR = PIN_ACT;
    PORTB.OUTSET = PIN_MODE;     /* restore steady MODE indicator */

    uart_puts("[M3] done hits=");
    uart_hex8(hits);
    uart_puts("\r\n");
}

/* ====================================================================== */
/*  SW1 multi-press handler                                                */
/* ====================================================================== */
/*
 * Entered after the first press has been debounced.  Wait for that first
 * release, then open a fixed MODE_WINDOW_MS window during which any
 * additional press increments the count (clamped to 3).
 */
static void handle_sw1(void)
{
    uint8_t presses = 1;

    /* Wait for the initial release. */
    while (!(PORTB.IN & PIN_SW1))
        ;
    _delay_ms(15);

    for (uint16_t t = 0; t < MODE_WINDOW_MS; t++) {
        _delay_ms(1);
        if (!(PORTB.IN & PIN_SW1)) {
            _delay_ms(15);
            if (!(PORTB.IN & PIN_SW1) && presses < 3) {
                presses++;
                while (!(PORTB.IN & PIN_SW1))
                    ;
                _delay_ms(15);
            }
        }
    }

    g_mode = presses;
    confirm_mode(g_mode);
}

/* ====================================================================== */
/*  Clock init                                                             */
/* ====================================================================== */
/*
 * Boot default: 20 MHz internal osc / 6 = 3.33 MHz.
 * We want 10 MHz (20 MHz / 2) for enough ISR headroom to snoop I2C in
 * Mode 3.  Writing MCLKCTRLB is CCP-protected.
 *
 * VCC = 3.0 V nominal on the badge; ATtiny1614 is spec'd for 10 MHz
 * down to 2.7 V, so this is inside envelope.
 */
static void clock_init(void)
{
    CCP = CCP_IOREG_gc;
    CLKCTRL.MCLKCTRLB = CLKCTRL_PDIV_2X_gc | CLKCTRL_PEN_bm;
}

/* ====================================================================== */
/*  Hardware init                                                          */
/* ====================================================================== */
static void hw_init(void)
{
    clock_init();

    /* LEDs as outputs, off */
    PORTA.DIRSET = PIN_ACT | PIN_HIT | PIN_ERR;
    PORTB.DIRSET = PIN_MODE;
    leds_off();

    /* Switches: input + internal pull-up */
    PORTA.PIN4CTRL = PORT_PULLUPEN_bm;       /* SW2 / PA4 */
    PORTB.PIN1CTRL = PORT_PULLUPEN_bm;       /* SW1 / PB1 */

    /* GPIO1 (PB3): output, idle low (will be retargeted as needed) */
    PORTB.OUTCLR = PIN_GPIO1;
    PORTB.DIRSET = PIN_GPIO1;

    /* GPIO2 (PB2): Hi-Z idle; pre-load OUT=0 so future DIRSET = open-drain
     * low assert with no glitch. */
    PORTB.OUTCLR = PIN_GPIO2;
    PORTB.DIRCLR = PIN_GPIO2;

    uart_init();
    uart_puts("\r\nDC34 SAO Fuzzer up\r\n");

    /* Power-on LED self-test */
    PORTA.OUTSET = PIN_ACT | PIN_HIT | PIN_ERR;
    PORTB.OUTSET = PIN_MODE;
    _delay_ms(200);
    leds_off();
    _delay_ms(100);

    confirm_mode(g_mode);
}

/* ====================================================================== */
/*  Main loop                                                              */
/* ====================================================================== */
int main(void)
{
    hw_init();

    for (;;) {
        /* SW1 -- mode cycle */
        if (!(PORTB.IN & PIN_SW1)) {
            _delay_ms(15);
            if (!(PORTB.IN & PIN_SW1))
                handle_sw1();
        }

        /* SW2 -- fire current attack */
        if (!(PORTA.IN & PIN_SW2)) {
            _delay_ms(15);
            if (!(PORTA.IN & PIN_SW2)) {
                /* Mode 2 acts while SW2 is held; modes 1/3 wait for release
                 * first so the user can let go before the run kicks off. */
                if (g_mode != 2) {
                    while (!(PORTA.IN & PIN_SW2))
                        ;
                    _delay_ms(15);
                }
                switch (g_mode) {
                case 1: attack_mode1(); break;
                case 2: attack_mode2(); break;
                case 3: attack_mode3(); break;
                default: break;
                }
            }
        }
    }
}
