#!/usr/bin/env python

# 2023 FIF orientering
# Tester for the serial buffet Pico
# Sends data to /dev/serial0 TxD (gpio pin 8), receives on RxD(gpio pin 10) and verifies that no data is missing
# RTS on gpio17 (pin 11) chokes the Rx to simulate a rate limited radio module
# Note: I could not get the HW RTS control on the RP400 to work, so a manual control is implementes on the RTS pin
#
# from pickle import FALSE
import time
import serial
import RPi.GPIO as gpio
# import sys
# import numpy as np
# import struct
import generatePunches

# List of tests, specifying the number of punches in each test
testList = [16, 128, 1024, 10*1024+10]
testList = [16, 16, 16, 16]
#testList = [2]
Npunches = 3200 					# Number of punches in the test
Nstations = 2 					# number of connected SI stations (input channels)

# HW Constants
CTS0channel = "16"
RTS0channel = "17"
RTS_ENABLE = gpio.LOW
RTS_DISABLE = gpio.HIGH
# PKG_LEN = 8 # Size of test punch without the two delimiters
rxCnt = 0

# Instantiate serial ports
# UART0 is used for both Tx and Rx
# UART2 is used for Tx to the second channel
# TBD UART list, for direct access by pointer
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

def receiveAll():
	rxCnt = 0
	rxBuffer = []	# Empty list of bytearrays to collect Rx
	while (True):
		readData = []
		gpio.output(17, RTS_ENABLE)  	# pulse-enable RTS
		time.sleep(0.001)
		gpio.output(17, RTS_DISABLE)
		time.sleep(0.01)				# Wait until tx done
		if (ser0.in_waiting == 0):		# Any chars received?
			break						# No, terminate Rx cycle
		while (ser0.in_waiting > 0):		# Yes, empty uart internal buffer
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
		i += punchLength 	# advance byte pointer to next punch 
		punches.append(punch)
		j += 1
	return (punches)

# Compare the received punches to the reference punches
def compareRxTx(responsePunches, stimulusPunches):
	identical=False
	inputIdx = 0
	if len(responsePunches) != len(stimulusPunches):
		print("*** Error: " + str(len(stimulusPunches)) + " punches sent, "\
							+ str(len(responsePunches)) + " punches received" ) 
		return(False)
	while inputIdx < len(responsePunches):			# Through input buffer
		responsePunch = responsePunches.pop(0) 	# Get reference punch
		stimulusfPunch = stimulusPunches.pop(0) 	# Get reference punch. TBD by mem pointer
		identical = (responsePunch == stimulusfPunch) # Verify integrity
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

# Flush the comm line for old buffer contents
print ("Flushing DUT")
dummyBuffer = receiveAll()
dummyBuffer = receiveAll()

# Test with varying fill levels of the buffer. 
# for a 10k char buffer plus UART internal buffers and 15 byte long punches,
#  we expect failure at  700 punchs Txed without Rx 
# so the last test loads the buffer to failure, validating the test.
for bufferLoad in testList:
	# Fill the test buffer
	stimulusPunches = generatePunches.generatePunches(bufferLoad, Nstations)
	print("\nTx started, " + str(bufferLoad) + " punches.")
	for txCount in range(bufferLoad):
		txPunch = stimulusPunches[txCount]
		# print("\rTx # " + str(txCount) + ": " + str(txPunch), end = '\r')
		# get channel to send on (AKA station number)
		punchCN = int.from_bytes(txPunch[3:5], 'big')
		#punchCN = 1 # Test: all punches to one channel
		#print ("Punch to ch ", punchCN ) 
		# Send punch
		transmitPunch(txPunch, punchCN) #TBD extract channel from punchSN
	# read the buffered data back and test integrity
	print ("\nRx started, expecting "  + str(bufferLoad) + " punchs.")
	# Empty the serialBuffer
	inputTestBuffer =	receiveAll() 
	if (len(inputTestBuffer) > 0):
		print("Input (" + str(len(inputTestBuffer)) + " chars): ", end = '\n')
		# Test for identity to txed data
		print('\nReceived buffer, comparing to reference')
	#	# Extract the individual punches from the received stream
		inputPunches = extractPunches(inputTestBuffer)
		if compareRxTx(inputPunches, stimulusPunches):  # Rx identical to Tx?
			print("\nTest with " + str(bufferLoad) + " punches terminated OK." ) # Yes
		else:
			print("\n*** Rx test error: Rx not equal Tx ***\n")	# No, exit with error.
			exit()		#
	else: # Nothing received
		print("\n*** Rx test Error: Nothing received!\n")	
print ("\nTest terminated.")
