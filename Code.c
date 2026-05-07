/*
 * Fire and Smoke Detection System
 * Target : ATmega328P (Arduino Uno) @ 16 MHz
 * Toolchain : AVR-GCC / Atmel Studio
 *
 * Pin mapping (same as original Arduino sketch):
 *   A0  (PC0) - MQ-2 smoke sensor (analog)
 *   D8  (PB0) - Flame sensor      (digital, active LOW)
 *   D9  (PB1) - Buzzer            (digital output)
 *   SDA (PC4) - I2C LCD (0x27)
 *   SCL (PC5) - I2C LCD (0x27)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Hardware constants                                                 */
/* ------------------------------------------------------------------ */
#define LCD_ADDR        0x27
#define LCD_COLS        16
#define LCD_ROWS        2

#define LCD_BL          (1<<3)
#define LCD_EN          (1<<2)
#define LCD_RW          (1<<1)
#define LCD_RS          (1<<0)

#define SMOKE_THRESHOLD 500          /* raised from 400 */
#define WARMUP_SECONDS  20

/* ------------------------------------------------------------------ */
/*  I2C (TWI) driver - 100 kHz                                        */
/* ------------------------------------------------------------------ */
static void twi_init(void)
{
    TWSR = 0x00;
    TWBR = (uint8_t)(((F_CPU / 100000UL) - 16) / 2);
    TWCR = (1<<TWEN);
}

static void twi_start(void)
{
    TWCR = (1<<TWINT)|(1<<TWSTA)|(1<<TWEN);
    while (!(TWCR & (1<<TWINT)));
}

static void twi_stop(void)
{
    TWCR = (1<<TWINT)|(1<<TWSTO)|(1<<TWEN);
    while (TWCR & (1<<TWSTO));
}

static void twi_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1<<TWINT)|(1<<TWEN);
    while (!(TWCR & (1<<TWINT)));
}

static void lcd_i2c_write(uint8_t byte)
{
    twi_start();
    twi_write((LCD_ADDR << 1) | 0);
    twi_write(byte);
    twi_stop();
}

/* ------------------------------------------------------------------ */
/*  LCD (HD44780 via PCF8574 I2C backpack)                            */
/* ------------------------------------------------------------------ */
static void lcd_pulse_enable(uint8_t data)
{
    lcd_i2c_write(data | LCD_EN | LCD_BL);
    _delay_us(1);
    lcd_i2c_write((data & ~LCD_EN) | LCD_BL);
    _delay_us(50);
}

static void lcd_send_nibble(uint8_t nibble, uint8_t flags)
{
    uint8_t data = (nibble << 4) | flags | LCD_BL;
    lcd_pulse_enable(data);
}

static void lcd_send_byte(uint8_t byte, uint8_t flags)
{
    lcd_send_nibble(byte >> 4,   flags);
    lcd_send_nibble(byte & 0x0F, flags);
    _delay_us(50);
}

static void lcd_cmd(uint8_t cmd) { lcd_send_byte(cmd, 0);      }
static void lcd_data(uint8_t ch) { lcd_send_byte(ch,  LCD_RS); }

static void lcd_init(void)
{
    _delay_ms(50);

    lcd_send_nibble(0x03, 0); _delay_ms(5);
    lcd_send_nibble(0x03, 0); _delay_ms(1);
    lcd_send_nibble(0x03, 0); _delay_us(150);
    lcd_send_nibble(0x02, 0); _delay_us(150);

    lcd_cmd(0x28);
    lcd_cmd(0x08);
    lcd_cmd(0x01);
    _delay_ms(2);
    lcd_cmd(0x06);
    lcd_cmd(0x0C);
    _delay_ms(2);
}

static void lcd_clear(void)
{
    lcd_cmd(0x01);
    _delay_ms(2);
}

static void lcd_set_cursor(uint8_t col, uint8_t row)
{
    uint8_t offsets[] = {0x00, 0x40};
    lcd_cmd(0x80 | (offsets[row] + col));
}

static void lcd_print(const char *str)
{
    while (*str)
        lcd_data((uint8_t)*str++);
}

static void lcd_clear_line(uint8_t row)
{
    lcd_set_cursor(0, row);
    for (uint8_t i = 0; i < LCD_COLS; i++)
        lcd_data(' ');
}

static void lcd_print_centered(const char *str, uint8_t row)
{
    uint8_t len = 0;
    while (str[len]) len++;
    uint8_t col = (LCD_COLS > len) ? (LCD_COLS - len) / 2 : 0;
    lcd_set_cursor(col, row);
    lcd_print(str);
}

/* ------------------------------------------------------------------ */
/*  ADC                                                                */
/* ------------------------------------------------------------------ */
static void adc_init(void)
{
    ADMUX  = (1<<REFS0);
    ADCSRA = (1<<ADEN)|(1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0);
}

static uint16_t adc_read(uint8_t channel)
{
    ADMUX  = (ADMUX & 0xF0) | (channel & 0x0F);
    ADCSRA |= (1<<ADSC);
    while (ADCSRA & (1<<ADSC));
    return ADC;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    DDRB  &= ~(1<<PB0);
    PORTB |=  (1<<PB0);
    DDRB  |=  (1<<PB1);
    PORTB &= ~(1<<PB1);

    twi_init();
    adc_init();
    lcd_init();

    /* --- Startup splash --- */
    lcd_clear();
    lcd_print_centered("FIRE AND SMOKE", 0);
    lcd_print_centered("DETECTION SYSTEM", 1);
    _delay_ms(2500);
    lcd_clear();

    /* ----------------------------------------------------------------
     * MQ-2 WARM-UP PHASE
     * The MQ-2 heating coil needs time to reach operating temperature.
     * During this window the buzzer is kept OFF and the LCD counts down
     * so the user knows the system is not ready yet.
     * ---------------------------------------------------------------- */
    lcd_print_centered("WARMING UP...", 0);

    char buf[17];
    for (uint8_t s = WARMUP_SECONDS; s > 0; s--)
    {
        /* Build "Ready in: XXs" centered on row 1 */
        sprintf(buf, "Ready in: %2us", s);
        lcd_clear_line(1);
        lcd_print_centered(buf, 1);
        _delay_ms(1000);
    }

    lcd_clear();
    lcd_print_centered("SYSTEM READY", 0);
    _delay_ms(1500);
    lcd_clear();

    /* --- State cache: forces a full LCD write on first loop pass --- */
    uint16_t prev_smoke_value  = 0xFFFF;   /* tracks real-time ADC value */
    uint8_t  prev_smoke_high   = 0xFF;
    uint8_t  prev_fire_detected = 0xFF;

    /* --- Main loop --- */
    while (1)
    {
        uint16_t smoke_value   = adc_read(0);
        uint8_t  flame_pin     = (PINB & (1<<PB0));

        uint8_t  smoke_high    = (smoke_value > SMOKE_THRESHOLD);
        uint8_t  fire_detected = (flame_pin == 0);

        /*
         * Row 0: "SMOKE:NNN HIGH" or "SMOKE:NNN LOW "
         * Redrawn whenever the ADC value or the HIGH/LOW state changes.
         * Format: "SMOKE:NNN HIGH" / "SMOKE:NNN LOW "  (16 chars max)
         *   col 0-5  : "SMOKE:"
         *   col 6-8  : 3-digit ADC value (0-1023 → fits in 4 chars; shown as 4 for padding)
         *   col 9    : space
         *   col 10-13: status
         */
        if (smoke_value != prev_smoke_value || smoke_high != prev_smoke_high)
        {
            lcd_clear_line(0);
            lcd_set_cursor(0, 0);
            /* "SMOKE:NNNN HIG" / "SMOKE:NNNN LOW" — max 15 chars, fits 16-col LCD */
            sprintf(buf, "SMOKE:%4u %s", smoke_value, smoke_high ? "HIGH" : "LOW ");
            lcd_print(buf);
            prev_smoke_value = smoke_value;
            prev_smoke_high  = smoke_high;
        }

        if (fire_detected != prev_fire_detected)
        {
            lcd_clear_line(1);
            lcd_set_cursor(0, 1);
            lcd_print(fire_detected ? "FIRE DETECTED   " : "NO FIRE DETECTED");
            prev_fire_detected = fire_detected;
        }

        if (smoke_high || fire_detected)
            PORTB |=  (1<<PB1);
        else
            PORTB &= ~(1<<PB1);

        _delay_ms(500);
    }

    return 0;
}
