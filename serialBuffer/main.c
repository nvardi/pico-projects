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
 * The LED shows a second of fast blinking on program start
 * Then turns on when receiving, off when sending chars, so it blinks on every punch.
 */

/// \tag::SerialBuffer[]

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "fifo.h"

#define Nchannels 2 

#define BAUD_RATE 38400
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

#define RX_QUEUE_SIZE 10*1024   // Queue for received punches as stream bytes
#define TX_QUEUE_SIZE 128       // Queue for tx-ready punches, one at a time (oversized))

long loopCount = 0;
uint8_t rx_char, im_char, tx_char;
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
// Definitions of the punch format
// Documentation: PC programmer's guide and SISRR1AP serial data record
// Each punch is a sequence of 17 to 18 chars
// Starting with an (optional) constant preamble byte, a constant header byte 
// and a length byte (always 13), then "length" payload bytes and two CRC bytes.
// We transfer all chars, but use the header and length to assemble complete punches for tx
// The length byte is respected to allow for future formats
// We attempt to transfer all data, even when the above format is not maintained.
const uint8_t punchPre 	= 0x02; 	// STX, constant preamble of punch (only in "new" format?)
const uint8_t punchHdr 	= 0xD3; 	// 211, Constant first byte of every punch

// States of the punch assembly and tx process:
// Look for a header, get the payload length, move the payload, transmit a punch
enum states {stateHeader, stateLength, statePayload, stateReady, stateTransmit};

// Define the channels
struct channelType {
    uart_inst_t *uart_id;
    int txGPIO;
    int rxGPIO;
    int ctsGPIO;
    int rtsGPIO;
    bool ctsEn;
    bool rtsEn;
    int chars_rxed;
    int chars_txed;    
    queue_t *rxQueue;    // Buffer for received data as chars
    queue_t *txQueue;   // buffer for one complete, ready to tx, punch
    int  state;
    int txLength;       // count of chars in current punch
};

struct channelType channel[Nchannels] = {
    [0].uart_id      = uart0,
    [1].uart_id      = uart1,
    [0].txGPIO   = 0,   // pin 1
    [1].txGPIO   = 4,   // pin 6
    [0].rxGPIO   = 1,   // pin 2
    [1].rxGPIO   = 5,   // pin 7
    [0].ctsGPIO  = 2,   // pin 4
    [1].ctsGPIO  = 6,   // pin 9
    [0].ctsEn   = true,
    [1].ctsEn   = true,
    [0].rtsEn   = false,
    [1].rtsEn   = false,
    [0].chars_rxed = 0,v g
    [1].chars_txed = 0,
    [0].chars_rxed = 0,
    [1].chars_txed = 0,
    [0].txLength = 0,
    [1].txLength = 0,
    [0].state = stateHeader,
    [1].state = stateHeader
};

int main() {
    // Visual indication of running program
    printf ("Started Serial Buffer\n");
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    for (int i=0; i<5; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(130);
        gpio_put(LED_PIN, 0);
        sleep_ms(170);
    }

    // Initialisation
    queue_t rxQueue[Nchannels] = {
        {0, 0, RX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * RX_QUEUE_SIZE)},
        {0, 0, RX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * RX_QUEUE_SIZE)} };
    queue_t txQueue[Nchannels] = {
        {0, 0, TX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * TX_QUEUE_SIZE)},
        {0, 0, TX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * TX_QUEUE_SIZE)} };

    for (int chan=0; chan<Nchannels; chan++  ){  // through channels
        // Allocate the queue buffers
        channel[chan].rxQueue = &rxQueue[chan];
        channel[chan].txQueue = &txQueue[chan];

        // Set up UARTs with a basic baud rate.
        uart_init(channel[chan].uart_id , 2400);
        // Actually, we want a different speed
        // The call will return the actual baud rate selected, which will be as close as
        // possible to that requested
        uart_set_baudrate(channel[chan].uart_id  , BAUD_RATE);
        // Set the TX and RX pins by using the function select on the GPIO
        // Set datasheet for more information on function select
        gpio_set_function(channel[chan].txGPIO, GPIO_FUNC_UART);
        gpio_set_function(channel[chan].rxGPIO, GPIO_FUNC_UART);
        gpio_set_pulls(channel[chan].rxGPIO, true, false);
        gpio_set_function(channel[chan].ctsGPIO , GPIO_FUNC_UART);


        // Set UART flow control CTS only (the buffer is always ready to receive)
        uart_set_hw_flow(channel[chan].uart_id, channel[chan].ctsEn , channel[chan].rtsEn );

        // Set our data format
        uart_set_format(channel[chan].uart_id, DATA_BITS, STOP_BITS, PARITY);

        // Turn on FIFO's
        uart_set_fifo_enabled(channel[chan].uart_id, true);

    } // initialisation

    while (1) {   // eternal poll loop
        loopCount ++;
        for (int chan=0; chan<Nchannels; chan++  ){  // through channels
            // Polled Rx
            if (uart_is_readable(channel[chan].uart_id)) {              // Chars received in UART?
                rx_char = uart_getc(channel[chan].uart_id);             // Yes!, get from UART
                queue_write(channel[chan].rxQueue, (void*)rx_char);     // Push into rx queue
                channel[chan].chars_rxed++;
                gpio_put(LED_PIN, 1);                                   // Turn LED on
            } //Get chars from uart to rx queue 

            // Main FSM
            // Switches states to reflect the punch assembly process
            // while moving data from the rx buffer to the tx buffer.  
            switch (channel[chan].state) {
                case stateHeader:   // Looking for the header byte
                    if (channel[chan].rxQueue->head != channel[chan].rxQueue->tail) {     // chars in rx queue?
                        im_char = (uint8_t)queue_read(channel[chan].rxQueue);           // Yes! Pop char from rx queue
                        queue_write(channel[chan].txQueue, (void*)im_char);             // Push char to tx queue 
                        channel[chan].txLength++;                                       // count up
                        if (channel[chan].txLength >= TX_QUEUE_SIZE) {                  // Tx queue filled (error!)?
                                channel[chan].state = stateTransmit;                    // Yes! Send as is
                        } else if (im_char == punchHdr) {                               // No! Detected  header?
                            channel[chan].state = stateLength;                          // yes, Get length 
                        } 
                    } // chars in rx queue
                break;
                case stateLength:   // Reading payload length
                    if (channel[chan].rxQueue->head != channel[chan].rxQueue->tail) {   // chars in rx queue
                        im_char = (uint8_t)queue_read(channel[chan].rxQueue);           // Pop char from rx queue
                        queue_write(channel[chan].txQueue, (void*)im_char);             // Push char to tx queue 
                        channel[chan].txLength++; // count up
                        if (channel[chan].txLength + im_char + 2 >= TX_QUEUE_SIZE) {    // Tx queue filled (tbd error)?
                            channel[chan].state = stateTransmit;                        // Yes! Send as is
                        } else {
                            // Set punch length, adding 3 (16 bit CRC  and stop char)
                            channel[chan].txLength = (im_char + 3);                     // Get length, adding 2 CRC bytes
                            channel[chan].state = statePayload;                         // Start transfer to tx queue
                        }   // update tx length
                    }   // rx queue not empty
                break;
                case statePayload: // Transferring payload rx to tx queue
                    if (channel[chan].rxQueue->head != channel[chan].rxQueue->tail) {   // chars in rx queue?
                        im_char = (uint8_t)queue_read(channel[chan].rxQueue);           // Yes! Pop char from rx queue
                        queue_write(channel[chan].txQueue, (void*)im_char);             // Push char to tx queue 
                        channel[chan].txLength--;                                       // count payload down
                        if (channel[chan].txLength == 0 ) {                             // last char transferred?
                            channel[chan].state = stateReady;                           // Yes! flag ready to transmit
                        }  // last char transferred
                    } // rx queue not empty
                break;
                case stateReady:   // Ready to transmit a punch
                channel[chan].txLength = 0;                         // Reset punch length
                    bool anyTx = false;
                    for (int c=0; c<Nchannels; c++  ){              // through channels
                        if (channel[c].state == stateTransmit){     // Any channel transmitting?
                            anyTx = true;                           // Yes! continue waiting
                        }
                    }   // thru channels 
                    if (!anyTx) {
                        channel[chan].state = stateTransmit;    // No! start transmission 
                    }   // Any channel transmitting   
                break;
                case stateTransmit:   // Transmitting a punch, do not fill tx queue
                    if (channel[chan].txQueue->head == channel[chan].txQueue->tail){    // Tx queue empty?
                        channel[chan].state = stateHeader;                              // Yes! Terminate transmit and look for header
                    } 
                break;
                default:
                break;
            } // switch channelState

            // // Polled Tx
            if(channel[chan].state == stateTransmit) {   // in transmit mode?
                if  (uart_is_writable(channel[0].uart_id) && 
                    (channel[chan].txQueue->head != channel[chan].txQueue->tail)) { // OK to tx? 
                    tx_char = (uint8_t)queue_read(channel[chan].txQueue);           // Yes! pop char
                    uart_putc(channel[0].uart_id, tx_char);                      // write to UART
                    channel[chan].chars_txed++;                                     // Count tx
                    gpio_put(LED_PIN, 0);                                           // LED off
                } // char transmission 
            } // stateTransmit, transmitting to UART
        } // thru channels
    } // poll loop
} // main loop

/// \end:SerialBuffer[]
