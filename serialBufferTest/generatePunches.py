import os
import time
import numpy as np

# Documentation: PC programmer's guide and SISRR1AP serial data record
# Each punch is a byte array of 15 bytes
punchPre 	= 0x02 	# STX, constant preamble of punch (only in "new" format?)
punchHdr 	= 0xD3 	# 211, Constant first byte of every punch
punchLen 	= 13 			# LEN 1 byte payload length byte, 0Dh = 13 byte constant
punchCN		= 0				#CN1, CN0 2 bytes stations code number 1...999 (in test:channel no.)
punchSN		= 0				# SN3...SN0 	= []	# 4 bytes SI-Card number (in test: enumeration)
punchTD		= 0				# TD 1 byte day-of-week/half day (in test: real time)
									# bit5...bit4 4 week counter relative
									# bit3...bit1 day of week
									# 000b Sunday
									# 001b Monday
									# 010b Tuesday
									# 011b Wednesday
									# 100b Thursday
									# 101b Friday
									# 110b Saturday
									# bit0 24h counter (0-am, 1-pm)
punchTmr	= 0				# TH...TL 2 bytes 12h timer, binary, seconds within AM/PM
punchTSS	= 0				# TSS 1 byte sub second values 1/256 sec
punchMem	= 0				# MEM2...MEM0 3 bytes backup memory start address of the data record (in test: index into punchList)
punchCRC	= 0xABCD		# CRC1, CRC0 2 bytes 16 bit CRC value, computed including command byte and in test: constant 0



# punch generator
# Fills a buffer with punch packages.

def generatePunches(Npunches=1, Nstations=2):
	os.system('clear')
	# Test constants
	testStart_ns = time.time_ns() 	# ref time in ns. TBD: midnight in ns
	testStart = time.localtime(testStart_ns/1e9)
	midnight_ns = testStart_ns - testStart.tm_sec*1e9 - 60*testStart.tm_min*1e9 - 60*60*testStart.tm_hour*1e9
	midnight = time.localtime(midnight_ns/1e9)

	# Fill the test buffer with punches to send
	# punchElementNames = ["            ", "STX ", "CMD ", "LEN  ", "CN1 ", "CN0 ", "SN3 ", "SN2 ", "SN1 ", \
	#	"SN0 ", "TD  ", "Tmr1 ", "Tmr0 ", "TSS ", "Mem2 ", "Mem1 ", "Mem0 ", "CRC1", "CRC0"]
	#print(" ".join('{:5s}'.format(s) for s in punchElementNames))
	punchList = [] # empty list of punches

	for punchIdx in range(Npunches):
		punchTime_ns = time.time_ns()
		punchTime = time.localtime(punchTime_ns/1e9)
		punch = []
		punch.extend([punchPre])
		punch.extend([punchHdr])
		punch.extend([punchLen])
		punchCN = np.random.randint(0,Nstations) 	# Station number randomized
		punch.extend(np.int16(punchCN).tobytes()[::-1])
		punch.extend(np.int32(punchIdx).tobytes()[::-1]) 	# SI card number (simple serial)
		punchTD = 16*(punchTime.tm_wday + 1) 			 	# bit 7..4: Weekday  
		punchTD += 2*(punchTime.tm_wday+1) 					# bit 3..1: Weekday, 
		punchTD += int(punchTime.tm_hour/12) # Bit 0: AM/PM
		punch.extend(np.int8(punchTD).tobytes()[::-1])
		punchTmr = np.mod((punchTime_ns - midnight_ns)/1e9, 12*60*60)  # sec within AM/PM
		punch.extend(np.int16(punchTmr).tobytes()[::-1])
		punchTSS = 	np.mod((punchTime_ns - midnight_ns)*256/1e9, 256)   # ns fraction 256ths
		punch.extend(np.int8(punchTSS).tobytes()[::-1]) 
		punch.extend(np.int32(punchIdx).tobytes()[::-1][1:4]) 	# Index of punch in punchList, 3 bytes
		punch.extend(np.int16(punchCRC).tobytes()[::-1]) 		#TBD
		# print("Punch " + '{:4d}'.format(punchSN) + " : " +  "    ".join('{:02x}'.format(byte) for byte in punch))
		punchList.append(punch)
		# punchSN += 1
	#print(" ".join('{:5s}'.format(s) for s in punchElementNames))
	print (str(len(punchList)) + " punches ready in test buffer.") 
	return(punchList)
	