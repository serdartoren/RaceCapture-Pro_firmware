/*
    FreeRTOS V7.6.0 - Copyright (C) 2013 Real Time Engineers Ltd.
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that has become a de facto standard.             *
     *                                                                       *
     *    Help yourself get started quickly and support the FreeRTOS         *
     *    project by purchasing a FreeRTOS tutorial book, reference          *
     *    manual, or both from: http://www.FreeRTOS.org/Documentation        *
     *                                                                       *
     *    Thank you!                                                         *
     *                                                                       *
    ***************************************************************************

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>!AND MODIFIED BY!<< the FreeRTOS exception.

    >>! NOTE: The modification to the GPL is included to allow you to distribute
    >>! a combined work that includes FreeRTOS without being obliged to provide
    >>! the source code for proprietary components outside of the FreeRTOS
    >>! kernel.

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available from the following
    link: http://www.freertos.org/a00114.html

    1 tab == 4 spaces!

    ***************************************************************************
     *                                                                       *
     *    Having a problem?  Start by reading the FAQ "My application does   *
     *    not run, what could be wrong?"                                     *
     *                                                                       *
     *    http://www.FreeRTOS.org/FAQHelp.html                               *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org - Documentation, books, training, latest versions,
    license and Real Time Engineers Ltd. contact details.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.OpenRTOS.com - Real Time Engineers ltd license FreeRTOS to High
    Integrity Systems to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

    1 tab == 4 spaces!
*/

/*
Changes from V3.0.0
	+ ISRcode is pulled inline and portTICKisr() is therefore
	  deleted from this file.

	+ Prescaler logic for Timer1 added to allow for a wider
	  range of TickRates.

Changes from V3.0.1
*/

#include <FreeRTOS.h>
#include <task.h>

/* IO port constants. */
#define portBIT_SET		(1)
#define portBIT_CLEAR	(0)

/*
 * Hardware setup for the tick.
 * We use a compare match on timer1. Depending on MPU-frequency
 * and requested tickrate, a prescaled value with a matching
 * prescaler are determined.
 */
#define	portTIMER_COMPARE_BASE			((APROCFREQ/4)/configTICK_RATE_HZ)

#if portTIMER_COMPARE_BASE   < 0x10000
#define	portTIMER_COMPARE_VALUE		(portTIMER_COMPARE_BASE)
#define portTIMER_COMPARE_PS1		(portBIT_CLEAR)
#define portTIMER_COMPARE_PS0		(portBIT_CLEAR)
#elif portTIMER_COMPARE_BASE < 0x20000
#define	portTIMER_COMPARE_VALUE		(portTIMER_COMPARE_BASE / 2)
#define portTIMER_COMPARE_PS1		(portBIT_CLEAR)
#define portTIMER_COMPARE_PS0		(portBIT_SET)
#elif portTIMER_COMPARE_BASE < 0x40000
#define	portTIMER_COMPARE_VALUE		(portTIMER_COMPARE_BASE / 4)
#define portTIMER_COMPARE_PS1		(portBIT_SET)
#define portTIMER_COMPARE_PS0		(portBIT_CLEAR)
#elif portTIMER_COMPARE_BASE < 0x80000
#define	portTIMER_COMPARE_VALUE		(portTIMER_COMPARE_BASE / 8)
#define portTIMER_COMPARE_PS1		(portBIT_SET)
#define portTIMER_COMPARE_PS0		(portBIT_SET)
#else
#error "TickRate out of range"
#endif

/*-----------------------------------------------------------*/

/*
 * Setup a timer for a regular tick.
 */
void portSetupTick( void )
{
        /*
         * Interrupts are disabled when this function is called.
         */

        /*
         * Setup CCP1
         * Provide the tick interrupt using a compare match on timer1.
         */

        /*
         * Set the compare match value.
         */
        CCPR1H = ( unsigned char ) ( ( portTIMER_COMPARE_VALUE >> 8 ) & 0xff );
        CCPR1L = ( unsigned char )   ( portTIMER_COMPARE_VALUE & 0xff );

        /*
         * Set Compare Special Event Trigger Mode
         */
        bCCP1M3 	= portBIT_SET;
        bCCP1M2 	= portBIT_CLEAR;
        bCCP1M1 	= portBIT_SET;
        bCCP1M0		= portBIT_SET;

        /*
         * Enable CCP1 interrupt
         */
        bCCP1IE 	= portBIT_SET;

        /*
         * We are only going to use the global interrupt bit, so disable
         * interruptpriorities and enable peripheral interrupts.
         */
        bIPEN		= portBIT_CLEAR;
        bPEIE		= portBIT_SET;

        /*
         * Set up timer1
         * It will produce the system tick.
         */

        /*
         * Clear the time count
         */
        TMR1H = ( unsigned char ) 0x00;
        TMR1L = ( unsigned char ) 0x00;

        /*
         * Setup the timer
         */
        bRD16		= portBIT_SET;				// 16-bit
        bT1CKPS1	= portTIMER_COMPARE_PS1;	// prescaler
        bT1CKPS0	= portTIMER_COMPARE_PS0;	// prescaler
        bT1OSCEN	= portBIT_SET;				// Oscillator enable
        bT1SYNC		= portBIT_SET;				// No external clock sync
        bTMR1CS		= portBIT_CLEAR;			// Internal clock

        bTMR1ON		= portBIT_SET;				// Start timer1
}
