/**
 * Copyright (c) 2022 FIF orientering.
 * 
 * Serial buffer, dual channel version
 * Buffers serial data from UART0 and UART1, to facilitate interfacing a transmitter without flow control
 * to a slow receiver with flow control.
 * All characters are relayed as received.
 * The CTS input from the radio module is respected, stopping the Tx until released
 * No flow control toward the SRRs 
 * Simple polled loop, continuously receiving bytes, sending complete contiguous punches
 * Interleaving punches from N stations. 
 */

/// \tag::SerialBuffer[]

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "fifo.h"

#define BAUD_RATE 38400
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

// We are using pins 0 and 1, but see the GPIO function select table in the
// datasheet for information on which other pins can be used.
//  Note: the numbers in these definitions are GPIO numbers, not physical pins.
#define UART0_ID uart0
#define UART1_ID uart1
#define UART0_TX_PIN 0
#define UART0_RX_PIN 1
#define UART0_CTS_PIN 2
#define UART1_TX_PIN 4
#define UART1_RX_PIN 5
#define UART1_CTS_PIN 6

static int chars_rxed, chars_txed, txLength = 0;
#define RX_QUEUE_SIZE 10*1024   // Queue for received punches as stream bytes
#define TX_QUEUE_SIZE 1024       // Queue for tx-ready punches, one at a time

uint8_t rx_char, im_char, tx_char;
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
// Definitions of the punch format
// Documentation: PC programmer's guide and SISRR1AP serial data record
// Each punch is a byte array of 14 or 15 bytes
// Starting with an (optional) constant preamble byte, a constant header byte 
// and a length byte (always 13), then "length" payload bytes and two CRC bytes.
// We transfer all chars, but use the header and length to assemble complete punches for tx
const uint8_t punchPre 	= 0x02; 	// STX, constant preamble of punch (only in "new" format?)
const uint8_t punchHdr 	= 0xD3; 	// 211, Constant first byte of every punch

// States of the punch assembly and tx process:
// Look for a header, get the payload length, move the payload, transmit a punch
enum states {stateHeader, stateLength, statePayload, stateTx} channelState = stateHeader;

int main() {

    printf("Started Serial Buffer\n");
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    for (int i=0; i<10; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(50);
        gpio_put(LED_PIN, 0);
        sleep_ms(50);
    }

    // Allocate the queue buffers
    queue_t rxQueue = {0, 0, RX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * RX_QUEUE_SIZE)};
    queue_t txQueue = {0, 0, RX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * TX_QUEUE_SIZE)};

    // Set up our UART with a basic baud rate.
    uart_init(UART0_ID, 2400);
    uart_init(UART1_ID, 2400);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART0_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART0_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART0_CTS_PIN , GPIO_FUNC_UART);
    gpio_set_function(UART1_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART1_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART1_CTS_PIN , GPIO_FUNC_UART);

    // Actually, we want a different speed
    // The call will return the actual baud rate selected, which will be as close as
    // possible to that requested
    int __unused uart0_actual_baud_rate = uart_set_baudrate(UART0_ID, BAUD_RATE);
    int __unused uart1_actual_baud_rate = uart_set_baudrate(UART1_ID, BAUD_RATE);

    // Set UART flow control CTS only (the buffer is always ready to receive)
    uart_set_hw_flow(UART0_ID, true, false);
    uart_set_hw_flow(UART1_ID, true, false);

    // Set our data format
    uart_set_format(UART0_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_format(UART1_ID, DATA_BITS, STOP_BITS, PARITY);

    // Turn on FIFO's
    uart_set_fifo_enabled(UART0_ID, true);
    uart_set_fifo_enabled(UART1_ID, true);

    // OK, all set up.
    // uart_puts(UART0_ID, "\nSerialBuffer started\n");
    while (1) {
        // Polled Rx
        if (uart_is_readable(UART1_ID)) {            // Chars received in UART?
            rx_char = uart_getc(UART1_ID);           // Yes!, get from UART
            queue_write(&rxQueue, (void*)rx_char);  // Push into rx queue
            chars_rxed++;
            gpio_put(LED_PIN, 1);                   // Turn LED on
        } //Get chars from uart to rx queue 

        // Main FSM
        // Switches states to reflect the punch assembly process
        // while moving data from the rx buffer to the tx buffer.  
        switch (channelState) {
            case stateHeader:   // Looking for the header byte
                if (rxQueue.head != rxQueue.tail) {             // chars in rx queue
                   im_char = (uint8_t)queue_read(&rxQueue);     // Pop char from rx queue
                    queue_write(&txQueue, (void*)im_char);      // Push char to tx queue 
                    txLength++;                                 // count up
                    if (txLength >= TX_QUEUE_SIZE) {            // Tx queue filled (error!)?
                            channelState = stateTx;             // Yes! Send as is
                    } else if (im_char == punchHdr) {       // No1 detected  header?
                       channelState = stateLength;            // yes, Get length 
                    } 
                } // rx queue not empty
            break;
            case stateLength:   // Reading payload length
                if (rxQueue.head != rxQueue.tail) {             // chars in rx queue
                    im_char = (uint8_t)queue_read(&rxQueue);     // Pop char from rx queue
                    queue_write(&txQueue, (void*)im_char);      // Push char to tx queue 
                    txLength++; // count up
                    if (txLength + im_char + 2 >= TX_QUEUE_SIZE) {    // Tx queue filled (error!)?
                        channelState = stateTx;         // Yes! Send as is
                    } else {
                        txLength = (im_char + 2);       // Get length, adding 2 CRC bytes
                        channelState = statePayload;    // Start transfer to tx queue
                    }
                }   // rx queue not empty
            break;
            case statePayload: // Transferring payload rx to tx queue
                if (rxQueue.head != rxQueue.tail) {             // chars in rx queue?
                    im_char = (uint8_t)queue_read(&rxQueue);    // Yes! Pop char from rx queue
                    queue_write(&txQueue, (void*)im_char);      // Push char to tx queue 
                    txLength--;                                 // count payload down
                    if (txLength == 0) {                        // last char transferred?
                        channelState = stateTx;                 // Yes! transmit tx queue
                    }
                } // rx queue not empty
            break;
            case stateTx:   // Transmitting a punch, do not fill tx queue
                txLength = 0;                       // Reset punch length
                if (txQueue.head == txQueue.tail){  // Tx queue empty?
                    channelState = stateHeader;     // Yes! Terminate transmit and look for header
                } 
            break;
            default:
            break;
        } // switch channelState

        // // Polled Tx
        if(channelState == stateTx) { // in transmit mode?
            if (uart_is_writable(UART0_ID) && (txQueue.head != txQueue.tail)) { // OK to tx? 
                tx_char = (uint8_t)queue_read(&txQueue);    // Yes! pop char
                uart_putc(UART0_ID, tx_char);                // write to UART
                chars_txed++;                               // Count tx
                gpio_put(LED_PIN, 0);                       // LED off
            } // char transmission 
        } // stateTx, transmitting to UART
    } // poll loop
} // main loop

/// \end:SerialBuffer[]
