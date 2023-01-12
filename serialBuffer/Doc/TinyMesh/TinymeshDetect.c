/******************************************************************
* Test version of software to detect on what USB connctor emulating 
* RS-232 lines on a Raspberry Pi there are Tinymesh radios connected.
*
* The flow of the software is:
* - Scan over the four possible USB ports on the Rasp, ttyUSB0 to ttyUSB3 
*   - try to open connection to port on 'hi' baud rate, if fails exit
*   - test if CTS signal on port is TRUE, if not exit
*   - send a short string of characters to port, if fails, exit
*   - test for CTS signal going FALSE within 10 to 100 msec, if fails, exit
*   - test for CTS signal going true within 20 to 500 msec, if fails, exit
*   - if this point is reached the suggest that a Tnymesh unit is connected.
*
*   - if neccesary, repeat the loop with both baud rates of SPORTident, 
*     38400 bps for new units and 4800 bps for old units. N.B. This interface 
*     baud rate is not related to the rate on the radio link
*   
* The software is using the RS-232 libratry from teuniz.net 
* The *.c and *.h files shall be in the working directory

Remember to incliude the source file, compile with acommand like:
gcc TinymeshDetect.c rs232.c -Wall -Wextra -o2 -o testtinymeshdetect

**********************************************************************/

/*
Node:	   Lenovo Desktop
Drive:	D: (old Win8.x disk)
Path:	   \Orientering\OnlineControls\Simsalabim\
File:	   TestTinymeshDetect
Auth:	   2021-01-05 @ 22:46 /Chris Bagge 
Vers:	   0.01 2021-01-05 @ 22:46 /cbagge template created
	      0.02 2021-01-10 @ 13:46 /cbagge initial version
	      0.03 2021-01-10 @ 14:48 /cbagge version able to detect gateway 
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>		// to be able to use timers
#include <unistd.h>

/* the serial library, must be in the search path */
#include "rs232.h"


/* global variables */

	int 	i;
   int 	n;
	int 	cport_nr = 16;			// actual port
	const int min_port = 16 ;		// corresponds to ttyUSB0
	const int max_port = 19 ;		// corresponds to ttyUSB3
	int	bdrate = 38400;			// 38400 baud the expected baudrate on the TinyMesh / SPORTident units
//  	int	bdrate = 4800;			// 4800 baud the alternate baudrate 
  	char	mode[]={'8','N','1',0};		// set to 8 bit, no parity, 1 stop bit
   const int noCTS = 0;			// no internal CTS handshake
   int	detectState;			// state of the frame transfer flow
   int	detectStatus = 0;		// status when leaving the detect state machine
   	
/* timer windows this should be more correct the using define? 
 * defines the timer window for clearing and setting the CTS flag 
 */

	const int startWinMin = 20;		// minimum time in mesc before the CTS goes false
	const int startWinMax = 100;		// maximum time in msec before the CTS goes false
	const int stopWinMin = 50;             // minimum time in msec the CTS is false
	const int stopWinMax = 250;            // maximum time in msec the CTS is false, router
	const int stopWinGwN = 400;            // maximum time in msec for CTS false, gateway with nodes
	const int stopWinGwA = 2000;          // maximum time in msec  the CTS false, gateway alone

   	
//   	unsigned char transBuff[128];		// buffer towards TinyMesh, it will send up to 120 byte messages
	/* the string to send to the device to see if it is possible */ 
   	unsigned char transBuf[12] = { 0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff, 0xff };
	unsigned char* pointer = transBuf;
  	int transBufSize=12 ;			// actual buffer size
  	int transferStatus ;	
        int transferState ;
 	struct timespec StartTime;      	// struct to hold system time information
 	struct timespec NowTime ;
 	
//	long int seconds;
//	long int useconds;

/* definitions of the actual state of the search*/

#define STATE_0 0			// state 0) before activity  	
#define STATE_A 1			// state_A) detect that CTS is true
#define STATE_B 2			// state_B) send data to device 
#define STATE_C 3			// state_C) waiting for CTS to go false, i.e. started transfer
#define STATE_D 4			// state_D) waiting for CTS to go true, i.e. transfer over 

/*     * the tranferStatus is:
       0 = Processing data
       1 = No device detected
       2 = Device detected, but CTS was not initially true
       3 = Could not write to the device detected 
       4 = Could not write all the data to device detected
       5 = CTS did not go false within time window given when starting to send. 
       6 = There was a timeout on waiting for CTS false
       7 = There was a long waiting for CTS true, probably Tinymesh gateway 
       8 = long CTS did not go true within the time window after sending
       9 = Tinymesh router detected ;-)
      */

/* function definitions */

/* get a start time for a series of actions, this to be able to calculate the 
 * the time some operations take. Uses the global variable (struct) StartTime
*/
 void getStartTime (void)
   {
   /* getting start time information */
      clock_gettime( CLOCK_REALTIME, &StartTime);
//      printf ( "Start time: %li sec and %li msec\n", StartTime.tv_sec, StartTime.tv_nsec/1000000 );
   }

 /* measure the duration of some event, i.e. get the time since last getStartTime call.
  * Depends on the global variable (struct) StartTime. It will get the time from a systems call 
  * and return the duration in milliseconds,. the value 1 msec is assumed to give a reasonale 
  * resolution. The receiver can reformat the response as needed.
  */
  
 int timeStamp (void) 
   
 {
    long int seconds ;
    long int useconds ;
    
    clock_gettime( CLOCK_REALTIME, &NowTime);
    seconds = NowTime.tv_sec - StartTime.tv_sec;
    useconds = (NowTime.tv_nsec - StartTime.tv_nsec)/1000;
    if (NowTime.tv_nsec < StartTime.tv_nsec)  // handle carry condition
       {
          seconds = seconds -1;
          useconds = useconds + 1000000;
       }
//   printf (" Timestamp: %li sec and %li msec\n", seconds, useconds/1000);
     return ((seconds * 1000) + (useconds/1000)) ;
 }

/* the detect state machine */

int main (void)
{
   int status=0;
//   int n = 0; //debug counter
   int time = 0 ; // timer value in msec
     

   /* initialize */
   cport_nr = min_port ;

   /* loop ower the possible COM ports */
   while (cport_nr <= max_port)
   {
      transferStatus = 0;
      transferState = STATE_0;
      /* here starts the detection as specified above. It is handles
       * as loop over a state machine, with a number of states the value
       * at exit determines whether or nor a Tinymesh was detected.
       */

      if (RS232_OpenComport(cport_nr, bdrate, mode, noCTS)) /* NoCTS = do not perform (internal) HW flow control */
      {
         printf("Port %i Cannot open comport \n", cport_nr);
         transferStatus = 1;
      }
      else /* there is a device search further */
      {
         printf("Port %i Check 1, COM port opened \n", cport_nr);
         transferState = STATE_A ;
         /* check that CTS by default is true */
         while (transferStatus == 0 ) /* looop until exit condition detected */
         {
            switch (transferState)  
            {
               case STATE_A: // port detected , see if CTS is true 
               {
                  status = RS232_IsCTSEnabled(cport_nr);
                  if (status != 0)	// CTS is true, go ahead
                  {
                     printf("Port %i Check 2, CTS initially true OK\n", cport_nr);
                     transferState = STATE_B;
                  }
                  else 
                  {
                     transferStatus = 2;	// exit search loop
                     printf ("Port %i   CTS not initially true\n", cport_nr);
                     printf ("Port %i   If router, check that mesh radio network is active\n", cport_nr);
                  }
                  break; 
               }      
               case STATE_B: //CTS was true, try to send data  
               {
 	          status = RS232_SendBuf (cport_nr, transBuf, transBufSize);
 	          if (status < 0) /* send error */ 
 	          {
                     transferStatus = 3;
                     printf ("Port %i   Cannot send data", cport_nr);
 	          }
 	          else if  (status != transBufSize) /* could not send all data */
 	         {
                   transferStatus = 4;
                     printf ("Port %i   Cannot send all data", cport_nr);
 	         }
 	         else /* A OK next step */
 	         {
                   printf("Port %i Check 3, able to send data OK\n", cport_nr);
 	           transferState = STATE_C ;
 	         }
 	   make      break;                 
               }
               case STATE_C: // could send data, try to wait for CTS going false, start transmission
               {
                  getStartTime ();
                  status = 1;
                  while (status != 0) /* wait for CTS false or timeout */
                  {
                     status = RS232_IsCTSEnabled(cport_nr);
                     time = timeStamp ();
 //                    printf ("*"); 
                     if (status == 0)	// CTS is false, go ahead
                     {
                        if ((time > startWinMin) && (time < startWinMax)) /* inside time window next step*/
                        {
                           transferState = STATE_D;
                           printf("Port %i Check 4, timing for CTS going false %i ms OK\n", cport_nr, time);
                        }
                        else  // timing window error
                        {
                           transferStatus = 5;
                           printf("Port %i   Time window error for CTS going false %i ms \n", cport_nr, time);
                        }
                     } // CTS is still true
                     else if (time >= startWinMax) /* CTS not yet false check timeout*/
                     {
                        transferStatus = 6;
                        status = 0; // to exit the loop
                        printf("Port %i   Timeout error for CTS going false %i ms\n", cport_nr, time);
                     }
                     /* else continue waiting */
                     usleep (1000);            
                  }
                  break ;
               }
               case STATE_D: // CTS is false when transmitting, try to wait for CTS true, end of transmission */
               {
 //                 getStartTime ();
                  status = 0;
                  while (status == 0) /* wait for CTS true or timeout */
                  {
                     status = RS232_IsCTSEnabled(cport_nr);
 //                    printf ("Status %i : ",status);
                     time = timeStamp (); 
                     if (status != 0)	// CTS is true, go ahead
                     {
                        if ((time > stopWinMin) && (time < stopWinMax)) /* inside time window next step */
                        {
                           transferStatus = 9;  /*Tinymesh detected */
                           printf("Port %i Check 5, timing for CTS going true  %i ms OK\n", cport_nr, time);
                           printf("Port %i    >>TinyMesh router detected !! \n", cport_nr) ;           
                        }
                        if ((time >= stopWinMax ) && ( time < stopWinGwN)) // timing window for active gateay
                        {
                           transferStatus = 7;
                           printf("Port %i Check 5, long time for CTS going true %i ms OK\n", cport_nr, time);
                           printf("Port %i    >>TinyMesh active gateway detected !! \n", cport_nr) ;           
                        }
                        if ((time >= stopWinGwN ) && ( time < stopWinGwA)) // timing window for lone gateay                   
                        {
                           transferStatus = 7;
                           printf("Port %i Check 5, long time for CTS going true %i ms OK\n", cport_nr, time);
                           printf("Port %i    >> TinyMesh active lonely gateway detected !! \n", cport_nr) ;           
                        }
                     }
                     if (time >= stopWinGwA) /* CTS not yet true check timeout */
                     {
                        transferStatus = 8;
                        printf("Port %i    CTS not turned true in %i ms \n", cport_nr, time) ;           
                        status = 1; // to exit the loop
                     }
                     /* else continue waiting */
                     usleep (10000);            
                  }
                  break ;
               }
            }  /* end of switch statement */
            usleep (1000); 	// sleep for 1 msec  
         } /* end of while transferStatus */
         RS232_CloseComport (cport_nr);
      } /* end of there was a port */
      cport_nr ++ ;
   }
   return 0;
}

