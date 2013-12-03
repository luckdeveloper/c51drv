/*  Copyright (c) 2013, laborer (laborer@126.com)
 *  Licensed under the BSD 2-Clause License.
 */


#include "common.h"
#include "uart.h"
#include "timer.h"


/* Ring buffer struct for UART I/O */
typedef struct {
    unsigned char dat[4];
    unsigned char in;
    unsigned char out;
} buffer_t;


/* Transmitting buffer */
static buffer_t txbuf;
/* Receiving buffer */
static buffer_t rcbuf;

/* Transmitting is turned off */
static char txoff;
/* Receiving buffer is overflow */
static char rcoff;


#ifdef UART_CALLBACK
void UART_CALLBACK(unsigned char c);
#endif /* UART_CALLBACK */

/* UART interrupt routine */
void uart_interrupt(void) __interrupt SI0_VECTOR __using 1
{
    unsigned char c;

    /* Check if UART is ready to read */
    if (RI) {
        /* Reset receiving interrupt flag */
        RI = 0;
        /* Read one byte from SBUF */
        c = SBUF;
        /* Check if receiving buffer is full */
        if (BUF_FULL(rcbuf)) {
            /* Set buffer overflow flag */
            rcoff = 1;
        } else {
            /* Read one byte from SBUF and put it in receiving
               buffer */
            BUF_PUT(rcbuf, c);
        }
#ifdef UART_CALLBACK
        UART_CALLBACK(c);
#endif /* UART_CALLBACK */
    }

    /* Check if UART is ready to write */
    if (TI) {
        /* Reset transmitting interrupt flag */
        TI = 0;
        /* Check if transmitting buffer is empty */
        if (BUF_EMPTY(txbuf)) {
            /* Turn off transmitting */
            txoff = 1;
        } else {
            /* Read a byte from transmitting buffer and put it in
               SBUF */
            SBUF = BUF_GET(txbuf);
        }
    }
}

/* Check if receiving buffer is empty */
char uart_rcempty(void)
{
    return BUF_EMPTY(rcbuf);
}

/* Read a byte from receiving buffer */
int uart_rcget(void)
{
    if (BUF_EMPTY(rcbuf)) {
        return -1;
    }
    return BUF_GET(rcbuf);
}

/* Send a byte to transmitting buffer */
char uart_txput(unsigned char c)
{
    if (BUF_FULL(txbuf)) {
        return -1;
    }

    BUF_PUT(txbuf, c);
    /* Check if transmitting is turned off */
    if (txoff) {
        /* Turn on transmitting */
        txoff = 0;
        /* Set transmitting interrupt flag to be ready */
        TI = 1;
    }
    return 0;
}

/* Send a byte in block mode */
void uart_putchar(unsigned char c)
{
    while (uart_txput(c)) {
        /* Enter idle mode */
        POWER_IDLE();
    }
}

/* Read a byte in block mode */
unsigned char uart_getchar(void)
{
    while (uart_rcempty()) {
        /* Enter idle mode */
        POWER_IDLE();
    }
    return uart_rcget();
}

/* Set a fixed baudrate */
void uart_baudrate(void)
{
    /* Set ratio for baud rate and oscillator frequency */
    /* SMOD1 SMOD0 - POF GF1 GF0 PD IDL
       1     -     - -   -   -   -  -   */
    PCON |= SMOD;

    /* Set timer */
    /* 256 - FOSC * (SMOD1 + 1) / BAUD / 32 / 12 */ 
    TIMER1_INIT8(256 - (unsigned char)(FOSC * (1 + 1) / UART_BAUD / 32 / 12));
    TIMER1_START();
}

/* Detect and set baudrate automatically  */
char uart_baudrate_auto(void)
{
    unsigned int        t;
    unsigned int        tmax;
    unsigned int        tmin;
    unsigned char       i;
    unsigned char       k;

    /* Disable Timer1 interrupt */
    ET1 = 0;
    /* Disable UART interrupt */
    ES = 0;

    /* Set Timer1 to be a 16-bit timer */
    TIMER1_INIT16();

    tmax = 0;
    tmin = 0xFFFF;
    /* k stores the number of successfully captured low level
       interval */
    k = 0;

    for (i = 0; i < 200; i++) {
        /* Wait RXD to be high */
        while (!RXD);
        /* Reset Timer1 */
        TIMER1_SET16(0);
        /* Reset Timer1 overflow flag */
        TIMER1_FLAG = 0;
        /* Resume Timer1 */
        TIMER1_START();
        /* Wait for a falling edge of RXD before Timer1 overflows */
        while (RXD && !TIMER1_FLAG);
        /* Reset Timer1 */
        TIMER1_SET16(0);
        /* Restart if it waited for too long */
        if (TIMER1_FLAG) {
            continue;
        }
        /* Wait for a rising edge of RXD */
        while (!RXD);
        /* Pause Timer1 */
        TIMER1_STOP();
        /* The baudrate is too low if Timer1 overflows */
        if (TIMER1_FLAG) {
            return -1;
        }

        /* Now we captured one low level interval */
        k += 1;

        /* Assume SMOD1=1 */
        /* t = (TH1 * 256 + TL1 + 8) / 16; */
        t = (TIMER1_GET16() + 8) >> 4;
        if (t > tmax) {
            tmax = t;
        }
        if (t > tmin && t < tmin * 2) {
            t -= tmin;
        }
        if (t < tmin) {
            tmin = t;
        }
        if (tmax >= tmin * 5) {
            k = 0xFF;
            break;
        }
    }

    if (tmin >> 8) {
        return -1;
    }
    if (k < 3) {
        return -2;
    }

    /* Set ratio for baud rate and oscillator frequency */
    /* SMOD1 SMOD0 - POF GF1 GF0 PD IDL
       1     -     - -   -   -   -  -   */
    PCON |= SMOD;

    /* Set timer */
    /* 256 - FOSC * (SMOD1 + 1) / BAUD / 32 / 12 */ 
    TIMER1_INIT8(256 - tmin);
    TIMER1_START();

    return 0;
}

/* Initialize UART */
void uart_init(void)
{
    BUF_INIT(rcbuf);
    BUF_INIT(txbuf);
    
    txoff = 1;
    rcoff = 0;

    /* Use Timer1 as baudrate generator */
    /* SM0 SM1 SM2 REN TB8 RB8 TI RI
       0   1   0   1   0   0   0  0  */
    SCON = 0x50;

    /* Enable UART interrupt */
    ES = 1;
    /* Enable global interrupt */
    EA = 1;
}

/* Send a string */
void uart_putstr(const unsigned char __code *s)
{
    for (; *s != 0; s++) {
        uart_putchar(*s);
    }
}

/* Send an unsigned int */
void uart_putuint(unsigned int i)
{
    unsigned char __idata       buf[5];
    unsigned char               j;
    
    uint2bcd(i, buf);
    for (j = 0; j < 4 && buf[j] == 0; j++);
    for (; j < 5; j++) {
        uart_putchar('0' + buf[j]);
    }
}

/* Send an int */
void uart_putint(int i)
{
    if (i < 0) {
        uart_putchar('-');
        i = -i;
    }
    uart_putuint(i);
}

