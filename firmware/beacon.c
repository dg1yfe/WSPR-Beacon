/*
 * Copyright 2018 Alexander Fasching
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdbool.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "uart.h"
#include "config.h"
#include "usbdrv.h"
#include "i2c_master.h"
#include "si5351.h"


#define CTRL_GET_REGISTER 0
#define CTRL_SET_REGISTER 1

#define REG_LED       0
#define REG_FREQ_CORR 1

/* The frequency is written to the output module when the most significant
 * word is written.
 */
#define REG_CLK0_FREQ_0          8
#define REG_CLK0_FREQ_1          9
#define REG_CLK0_ENABLE         10
#define REG_CLK0_DRIVE          11

#define STATUS_OK 0
#define STATUS_ERROR 1

char strbuf[64];

static inline void led_init(void) { DDRB |= (1<<PB5); }
static inline void led_on(void) { PORTB |= (1<<PB5); }
static inline void led_off(void) { PORTB &= ~(1<<PB5); }
static inline void led_toggle(void) { PORTB ^= (1<<PB5); }
static inline bool led_value(void) { return !!(PORTB & (1<<PB5)); }
static inline void led_set(bool v) { v ? led_on() : led_off(); }


typedef struct {
    uint16_t value;
    uint16_t status;
} response_t;


typedef struct {
    uint32_t frequency;
    uint8_t enabled;
    uint8_t drive;
} clk_output_t;

clk_output_t clk_outputs[3] = {0};

int16_t freq_corr = 0;


usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t *rq = (void *) &data[0];
    static response_t response;

    if (rq->bRequest == CTRL_GET_REGISTER) {
        uint16_t address = rq->wIndex.word;

        response.value = 0;
        response.status = STATUS_ERROR;

        switch (address) {
            case REG_LED:
                response.value = led_value();
                response.status = STATUS_OK;
                break;

            case REG_CLK0_FREQ_0:
                response.value = (clk_outputs[0].frequency >> 0) & 0xFFFF;
                response.status = STATUS_OK;
                break;

            case REG_CLK0_FREQ_1:
                response.value = (clk_outputs[0].frequency >> 16) & 0xFFFF;
                response.status = STATUS_OK;
                break;

            case REG_CLK0_ENABLE:
                response.value = clk_outputs[0].enabled;
                response.status = STATUS_OK;
                break;

            case REG_CLK0_DRIVE:
                response.value = clk_outputs[0].drive;
                response.status = STATUS_OK;
                break;

            case REG_FREQ_CORR:
                response.value = freq_corr;
                response.status = STATUS_OK;
                break;

        }

        usbMsgPtr = (void *) &response;
        return sizeof(response);

    } else if (rq->bRequest == CTRL_SET_REGISTER) {
        uint16_t address = rq->wIndex.word;
        uint16_t value = rq->wValue.word;

        response.value = 0;
        response.status = STATUS_ERROR;

        switch (address) {
            case REG_LED:
                led_set(value);
                response.value = !!value;
                response.status = STATUS_OK;
                break;

            case REG_CLK0_FREQ_0:
                clk_outputs[0].frequency &= 0xFFFF0000;
                clk_outputs[0].frequency |= ((uint32_t)value << 0UL);
                response.value = value;
                response.status = STATUS_OK;
                break;

            case REG_CLK0_FREQ_1:
                clk_outputs[0].frequency &= 0x0000FFFF;
                clk_outputs[0].frequency |= ((uint32_t)value << 16UL);
                si5351_set_freq(clk_outputs[0].frequency, SI5351_CLK0);
                response.value = value;
                response.status = STATUS_OK;
                sprintf(strbuf, "Setting frequency to %ld Hz\n", clk_outputs[0].frequency);
                uart_puts(strbuf);

                break;

            case REG_CLK0_ENABLE:
                clk_outputs[0].enabled = !!value;
                si5351_output_enable(SI5351_CLK0, !!value);
                response.value = !!value;
                response.status = STATUS_OK;

                if (value)
                    sprintf(strbuf, "Output enabled\n");
                else
                    sprintf(strbuf, "Output disabled\n");
                uart_puts(strbuf);

                break;

            case REG_CLK0_DRIVE:
                if (value > SI5351_DRIVE_8MA)
                    break;
                clk_outputs[0].drive = value;
                si5351_drive_strength(SI5351_CLK0, value);
                response.value = value;
                response.status = STATUS_OK;

                uint8_t strength = (value + 1) * 2;
                sprintf(strbuf, "Setting drive strength to %d mA\n", strength);
                uart_puts(strbuf);

                break;

            case REG_FREQ_CORR:
                freq_corr = value;
                si5351_set_correction(freq_corr);
                response.value = freq_corr;
                response.status = STATUS_OK;
                sprintf(strbuf, "Setting frequency correction to %d ppm\n", freq_corr / 10);
                uart_puts(strbuf);
                break;
        }

        usbMsgPtr = (void *) &response;
        return sizeof(response);
    }

    return 0;
}


int main(int argc, char **argv) {
    led_init();
    uart_init(UART_BAUD_SELECT(115200, F_CPU));

    i2c_init();

    /* Initialize the USB driver and force enumeration. */
    usbInit();
    usbDeviceDisconnect();
    _delay_ms(250);
    usbDeviceConnect();

    uart_puts("WSPR Beacon by OE5TKM\n");

    sei();

    si5351_init(SI5351_CRYSTAL_LOAD_8PF, SI5351_CLK_SRC_XTAL);
    si5351_drive_strength(SI5351_CLK0, SI5351_DRIVE_2MA);

    while (1) {
        usbPoll();
    }

    return 0;
}
