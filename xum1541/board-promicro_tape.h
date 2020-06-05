/*
 * Board interface routines for the Pro Micro
 * Copyright (c) 2015 Marko Solajic <msolajic@gmail.com>
 * Copyright (c) 2014 Thomas Kindler <mail_xum@t-kindler.de>
 * Copyright (c) 2009-2010 Nate Lawson <nate@root.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _BOARD_PROMICRO_TAPE_H
#define _BOARD_PROMICRO_TAPE_H

// Initialize the board (timer, indicators, UART)
void board_init(void);

// pinout is: PIN / PIN NAME ON BOARD

#define LED_MASK        _BV(5) // PD5 / GREEN ONBOARD LED
#define LED_PORT        PORTD
#define LED_DDR         DDRD

#define TAPE_SUPPORT    1
#define TAPE_COMMANDS_ONLY   1

// Define tape SENSE line
#define IO_SENSE        _BV(2) // B2/PCINT2
#define DDR_SENSE       DDRB
#define PORT_SENSE      PORTB
#define PIN_SENSE       PINB

// Define tape MOTOR CONTROL line
#define IO_MOTOR        _BV(1) // B1
#define DDR_MOTOR       DDRB
#define PORT_MOTOR      PORTB
#define PIN_MOTOR       PINB

// Define tape disconnect test lines
#define IO_DETECT_IN    _BV(0) // D0/INT0
#define IO_DETECT_OUT   _BV(1) // D1
#define DDR_DETECT      DDRD
#define PORT_DETECT     PORTD
#define PIN_DETECT      PIND
#define IN_EIFR         _BV(INTF0)            // EIFR: INT0 flag.
#define IN_EIMSK        _BV(INT0)             // EIMSK: INT0 mask.
#define IN_EICRA        _BV(ISC01)|_BV(ISC00) // Interrupt Sense Control: Rising edge of D0 generates interrupt request.

// Define tape READ line
#define IO_READ         _BV(4) // D4/ICP1
#define DDR_READ        DDRD
#define PORT_READ       PORTD

// Define tape WRITE line
#define IO_WRITE        _BV(5) // B5/OC1A
#define DDR_WRITE       DDRB
#define PORT_WRITE      PORTB

/*
 * Use always_inline to override gcc's -Os option. Since we measured each
 * inline function's disassembly and verified the size decrease, we are
 * certain when we specify inline that we really want it.
 */
#define INLINE          static inline __attribute__((always_inline))

// Status indicators (LEDs)
uint8_t board_get_status(void);
void board_set_status(uint8_t status);
void board_update_display(void);
bool board_timer_fired(void);

#endif // _BOARD_PROMICRO_TAPE_H
