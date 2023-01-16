#!/usr/bin/env python

# 2023 FIF orientering <Nitsan Vardi>
# Tester for the serial buffet Pico - dual channel version
# Sends data to /dev/serial0 and serial1 TxD (gpio pin 8, 27);
#  receives on dev/serial0 RxD(gpio pin 10) and verifies that no data is missing
# CTS on gpio17 (pin 11) chokes the Rx to simulate a rate limited radio module
# Note: I could not get the HW CTS control on the RP400 to work, 
# so a manual control is implementes on the RTS pin
#
import time
import serial
import RPi.GPIO as gpio
import os
# import sys
import numpy as np
# import struct
import generatePunches  # local file, function that simulates an SI station with SRR

# List of tests, specifying the number of punches in each test
testList = [16, 128, 1024, 10*1024+10] 
# The last test intentionally overflows the buffer, validating the test
# With 2 x 10K buffer, we expect about 1200 punches before overflow
testList = [16, 16, 16, 16]
#testList = [16]

Nstations = 2 					# number of connected SI stations (input channels)

# HW Constants
RTS_ENABLE = gpio.LOW
RTS_DISABLE = gpio.HIGH
rxCnt = 0

# Instantiate serial ports
# UART0 is used for both Tx and Rx
# UART2 is used for Tx to the second channel
ser0 = serial.Serial(
	port='/dev/serial0',
	baudrate = 38400,
	parity=serial.PARITY_NONE,
	stopbits=serial.STOPBITS_ONE,
	bytesize=serial.EIGHTBITS,
	timeout=1
)

# Channel 1 will use UART2, 
# Pinout: Tx:GPIO0, Rx:GPIO1, CTS:GPIO2, RTS:GPIO3; pins 27, 28, 3, 5 resp.
# Note: enable UART2 as dtoverlay in /boot/config.txt
ser1 = serial.Serial(
	port='/dev/ttyAMA1',
	baudrate = 38400,
	parity=serial.PARITY_NONE,
	stopbits=serial.STOPBITS_ONE,
	bytesize=serial.EIGHTBITS,
	timeout=1
)

# Transmitted test strings are saved for cmparison on Rx
stimulusPunches = []
inputTestBuffer = []

# Transmit function
# Transmits the "payload" string to "channel"  uart
def transmitPunch(payload, channel = 0):
	# write to serial port
	if channel == 0:
		#print("Send ch 0: ", payload, end = '\n')
		ser0.write(payload)
		while(ser0.out_waiting > 0): # Wait for the UART to empty its internal buffer
			time.sleep(0.001)		# Time for last char transmission
	else:
		#print("Send ch 1: ", payload, end = '\n')
		ser1.write(payload)
		while(ser1.out_waiting > 0): # Wait for the UART to empty its internal buffer
			time.sleep(0.001)		# Time for last char transmission
# End of transmitPunch

# Read serial0 channel as long as chars are received
# The CTS line is pulsed to enable a burst  
def receiveAll():
	rxBuffer = []	# Empty list of bytearrays to collect Rx
	while (True):
		readData = []					# Buffer for bytes read in each burst
		gpio.output(17, RTS_ENABLE)  	# pulse-enable RTS
		time.sleep(0.001)
		gpio.output(17, RTS_DISABLE)
		time.sleep(0.01)				# Wait until tx done
		if (ser0.in_waiting == 0):		# Any chars received in uart?
			break						# No, terminate Rx cycle
		while (ser0.in_waiting > 0):	# Yes, empty uart internal buffer
			readData = ser0.read()
			rxBuffer.extend(readData)
	return rxBuffer

# Extract the punches from the input stream to a list 
def extractPunches(punchStream):
	i = 0 # byte index in input stream
	j = 0 # punch index in output list
	punches = [] # Empty list of extracted punches
	while i < len(punchStream):	# Through input buffer
		#if punchStream[i] != generatePunches.punchHdr: # Classic, Header OK?
		if punchStream[i+1] != generatePunches.punchHdr: # New, Header OK?
			print("*** Error: punch header not found where expected") # No, flag error
			return (punches)
		punchLength = int(punchStream[i+2] +5) # Get punch length, add header, length and CRC 
		punch = punchStream[i : i + punchLength] # Extract punch. TBD CRC check
		# print("Extracted punch # " + str(j) + " :" + str(punch))
		i += punchLength 		# advance byte pointer to next punch 
		punches.append(punch)	# Push punch to array
		j += 1
	return (punches)

# Compare the received punches to the reference punches
# The reference punch list is ordered by serial no.
# The comparison extracts the punch according to the received punch serial number
# which is saved in the SI ID field.
def compareRxTx(responsePunches, stimulusPunches):
	identical=False
	inputIdx = 0
	if len(responsePunches) != len(stimulusPunches):
		print("*** Error: " + str(len(stimulusPunches)) + " punches sent, "\
							+ str(len(responsePunches)) + " punches received" ) 
		return(False)
	while inputIdx < len(responsePunches):			# Through input buffer
		responsePunch = responsePunches.pop(0) 	# Get response punch
		punchSNidx = int(responsePunch.index(generatePunches.punchHdr)) + 4 # locate serial no.
		stimulusIdx = int.from_bytes(responsePunch[punchSNidx:punchSNidx+4], 'big')
		stimulusPunch = stimulusPunches[stimulusIdx] 	# Get reference punch. by pointer
		identical = (responsePunch == stimulusPunch) # Verify integrity
		#print ('\033[K', end = '')   # clear line
		#print (f'{inputPunch:<12}' + " equals " + f'{refPunch:<12}' + " ? :" + f'{str(identical):<10}', end = "\r", flush=True)
		#sys.stdout.flush() # 
		#time.sleep(0.1)
		if (not identical):
			print ("*** Error in response punch # " + str(inputIdx))
			return(False)
	return (identical)

# Enable GPIO17 as an ordinary output (could not get the HW handshake to work in RP400)
gpio.setwarnings(False)
gpio.setmode(gpio.BCM)
gpio.setup(17, gpio.OUT)

os.system('clear')

# Flush the comm line for old buffer contents
print ("Flushing DUT")
dummyBuffer = receiveAll()
print ("DUT flushed: " + str(len(dummyBuffer)) + " bytes.")
dummyBuffer = receiveAll()
print ("DUT flushed: " + str(len(dummyBuffer)) + " bytes.")

# Test with varying fill levels of the buffer. 
# for a 10k char buffer plus UART internal buffers and 17 or 18 byte long punches,
#  we expect failure at  700 punchs Txed without Rx 
# so the last test loads the buffer to failure, validating the test.
for bufferLoad in testList:
	# Fill the test buffer
	stimulusPunches = generatePunches.generatePunches(bufferLoad, Nstations)
	print("Tx started, " + str(bufferLoad) + " punches.")
	for txCount in range(bufferLoad):
		txPunch = stimulusPunches[txCount]
		# print("\rTx # " + str(txCount) + ": " + str(txPunch), end = '\r')
		# get channel to send on (AKA station number)
		punchCN = int.from_bytes(txPunch[3:5], 'big')
		#punchCN = 0 # Test: all punches to one channel
		#punchCN = 1 # Test: all punches to one channel
		#print ("Punch to ch ", punchCN ) 
		# Send punch
		transmitPunch(txPunch, punchCN) #TBD extract channel from punchSN
	# read the buffered data back and test integrity
	print ("Rx started, expecting "  + str(bufferLoad) + " punchs.")
	# Empty the serialBuffer
	inputTestBuffer =	receiveAll() 
	if (len(inputTestBuffer) > 0):
		#print("Input (" + str(len(inputTestBuffer)) + " chars): ", end = '\n')
		# Test for identity to txed data
		print('Received buffer, comparing to reference')
	#	# Extract the individual punches from the received stream
		inputPunches = extractPunches(inputTestBuffer)
		if compareRxTx(inputPunches, stimulusPunches):  # Rx identical to Tx?
			print("Test with " + str(bufferLoad) + " punches terminated OK." ) # Yes
		else:
			print("*** Rx test error: Rx not equal Tx ***")	# No, exit with error.
			exit()		#
	else: # Nothing received
		print("*** Rx test Error: Nothing received!")	
print ("\nTest terminated.")
