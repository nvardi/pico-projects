/**
 * Copyright (c) 2022 FIF orientering.
 * 
 * Serial buffer
 * Buffers serial data from UART0 and UART2, 
 * to facilitate interfacing a transmitter without handshake to a slow receiver
 * Only complete punches are transmitted.
 * The CTS input is respected stopping the Tx until released 
 */

#include <stdio.h>
#include <string.h>
#include "stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "fifo.h"

/// \tag::SerialBuffer[]
#define Nstations 2         // Number of SRR input channels
#define punchHdr 0xD3     //punch start char 
#define UART0_ID uart0
#define UART1_ID uart2
#define BAUD_RATE 38400
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

// Define the channels
struct channelType {
    uart_inst_t *uart_id;
    int txPin;
    int rxPin;
    int ctsPin;
    int rtsPin;
    bool ctsEn;
    bool rtsEn;
    int chars_rxed;
    int chars_txed;    
    queue_t *rxQueue;     // Buffer for received data as chars
    queue_t *txQueue;    // buffer for one complete, ready to tx, punch
    enum state {stateStart, stateLength, statePayload, stateTx} state;
    int length; // count of chars in current punch
};

struct channelType channel[Nstations] = {
    [0].uart_id      = uart0,
    [1].uart_id      = uart1,
    [0].txPin   = 0, // pins defined by their GPIO number
    [1].txPin   = 4,
    [0].rxPin   = 1,
    [1].rxPin   = 5,    // unused
    [0].ctsPin  = 18,   // gpio18, tbd CTS, pin4
    [1].ctsPin  = 19,    // unused
    [0].ctsEn   = true,
    [1].ctsEn   = false,
    [0].rtsEn   = false,
    [1].rtsEn   = false,
    [0].chars_rxed = 0,
    [1].chars_txed = 0,
    [0].chars_rxed = 0,
    [1].chars_txed = 0,
    [0].state = stateStart,
    [1].state = stateStart
};


#define RX_QUEUE_SIZE 10*1024   // Size of Rx buffer in chars, accomodates "many" punches
#define TX_QUEUE_SIZE 128       // Size of tx buffer in chars, accomodates one punch 
uint8_t rx_ch, tx_ch;           // actual received and transmitted char
const uint LED_PIN = PICO_DEFAULT_LED_PIN;  
int txLock = Nstations; // Index of currently transmitting channel. None when Nstations


int main() {

    printf("Started Serial Buffer\n");
    gpio_init(LED_PIN);                 // Light the LED, TBD heartbeat by PWM
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);

    // initialize channels
    queue_t rxQueue[Nstations] = {  {0, 0, RX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * RX_QUEUE_SIZE)},
                                    {0, 0, RX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * RX_QUEUE_SIZE)}};
    queue_t txQueue[Nstations] = {  {0, 0, TX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * TX_QUEUE_SIZE)},
                                    {0, 0, TX_QUEUE_SIZE, malloc(sizeof(uint8_t*) * TX_QUEUE_SIZE)}};
    for (int chan = 0; chan < Nstations; chan++) {
        // Allocate the queue buffers and set pointers to them
        channel[chan].rxQueue = &rxQueue[chan];
        channel[chan].txQueue = &txQueue[chan];

        // Set up our UART with a basic baud rate.
        uart_init(channel[chan].uart_id, 2400);
        // Actually, we want a different speed
        // The call will return the actual baud rate selected, which will be as close as
        // possible to that requested
        int __unused actual = uart_set_baudrate(channel[chan].uart_id, BAUD_RATE);

        // Set the TX and RX pins by using the function select on the GPIO
        // Set datasheet for more information on function select
        gpio_set_function(channel[chan].txPin, GPIO_FUNC_UART);
        gpio_set_function(channel[chan].rxPin, GPIO_FUNC_UART);
        gpio_set_function(channel[chan].ctsPin, GPIO_FUNC_UART);


        // Set UART flow control CTS only (the buffer is always ready to receive, but respects the transmit handshake)
        uart_set_hw_flow(channel[chan].uart_id, channel[chan].ctsEn, channel[chan].rtsEn);

        // Set our data format
        uart_set_format(channel[chan].uart_id, DATA_BITS, STOP_BITS, PARITY);

        // Turn on the UART FIFO's
        uart_set_fifo_enabled(channel[chan].uart_id, true);

        // OK, all set up.
    } // channel setup

    uart_puts(channel[0].uart_id, "\nSerialBuffer started\n"); // TBR after initial test, do not confuse the receiver.

    while (1) {
        for (int chan = 0; chan < Nstations; chan++){
            // Polled Rx
            // Get chars from UARTs
            if (uart_is_readable(channel[chan].uart_id)) {                  // Data in uart?
                rx_ch = uart_getc(channel[chan].uart_id);                   // Yes! pop from uart
                queue_write(&rxQueue[chan], (void *)rx_ch);                 // Push into FIFO buffer
                channel[chan].chars_rxed++;                                 // Count Rx
                gpio_put(LED_PIN, 1);
            }

            if (
                (txLock != chan) && // Tx lock not mine?
                (channel[chan].state == stateTx) && // and Channel transmitting ?
                (txQueue[chan].head == txQueue[chan].tail)) { // and tx queue  empty ? 
                    channel[chan].state = stateStart; // Yes, unflag tx request 
            } else {        // No, check Rx queue for content
                if (rxQueue[chan].head != rxQueue[chan].tail) { // Anything in Rx queue?
                    uint8_t im_ch = (uint8_t)queue_read(&rxQueue[chan]); //  yes, pop a char from rx queue 
                    queue_write(&rxQueue[chan], (void*)im_ch); // Push to tx queue
                    switch (channel[chan].state) {  // Inspect the transferred char and update the channel's state
                        case stateStart:            // waiting for header
                            if (im_ch == punchHdr){  // Found header?
                                    channel[chan].state = stateLength;  // Yes! wait for length
                            }
                        break;
                        case stateLength:  // Waiting for length
                            channel[chan].length = im_ch + 2; // Initialise length counter, adding 2 for CRC
                            channel[chan].state = statePayload; // Collect payload and CRC (TBD CRC check?)
                        break;
                        case  statePayload:                         // Collecting payload
                            channel[chan].length-- ;                // Count length
                            if (channel[chan].length <= 0 )  {      // Payload & CRC transferred?
                                channel[chan].state = stateTx;      // Yes! flag "channel ready to tx"
                            }
                        break;
                        case stateTx: // Error, we never transfer rx to tx while sending from tx.
                        default:
                        break;
                    }   //    channel state
                } //  Anything in rx queue
            } // Channel transmitting?
        } // Thru channels

        // update tx lock
        // Tx lock is the number of the channel that currently sends a punch
        // If the lock's value points to an inexistent channel, it is free
        // The priority is simple by channel index 
        if (txLock >= Nstations ) {// Tx lock released?
            for (int chan = 0; chan < Nstations; chan++) {
                if ( channel[chan].state == stateTx ) {     // Channel has data to send
                        txLock = chan;  // Assign tx lock
                }
            }
        }   
        else {   // Tx locked to a channel
            if (txQueue[txLock].head == txQueue[txLock].tail)  // The transmitting channel's tx queue is empty
                txLock = Nstations; // Release tx lock
        } // Update Tx lock

        // Polled Tx only on channel 0
        if (uart_is_writable(channel[0].uart_id && txLock < Nstations)) {        // Tx ready and an Rx ready
            uint8_t tx_ch = (uint8_t)queue_read(channel[txLock].txQueue);       // Yes, send it
            uart_putc(channel[0].uart_id, tx_ch);
            channel[0].chars_txed++;
            // Is the active tx queue empty?
            // yes, release the tx lock
            gpio_put(LED_PIN, 0); // Turn LED off
        } // uart is writable
    } // poll loop
} // main

/// \end:SerialBuffer[]
