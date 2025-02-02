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

#include <stdlib.h>
#include <string.h>

/* Defining MPU_WRAPPERS_INCLUDED_FROM_API_FILE prevents task.h from redefining
all the API functions to use the MPU wrappers.  That should only be done when
task.h is included from an application file. */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#if ( configUSE_CO_ROUTINES == 1 )
#include "croutine.h"
#endif

/* Lint e961 and e750 are suppressed as a MISRA exception justified because the
MPU ports require MPU_WRAPPERS_INCLUDED_FROM_API_FILE to be defined for the
header files above, but not in this file, in order to generate the correct
privileged Vs unprivileged linkage and placement. */
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE /*lint !e961 !e750. */


/* Constants used with the cRxLock and xTxLock structure members. */
#define queueUNLOCKED					( ( signed portBASE_TYPE ) -1 )
#define queueLOCKED_UNMODIFIED			( ( signed portBASE_TYPE ) 0 )

/* When the xQUEUE structure is used to represent a base queue its pcHead and
pcTail members are used as pointers into the queue storage area.  When the
xQUEUE structure is used to represent a mutex pcHead and pcTail pointers are
not necessary, and the pcHead pointer is set to NULL to indicate that the
pcTail pointer actually points to the mutex holder (if any).  Map alternative
names to the pcHead and pcTail structure members to ensure the readability of
the code is maintained despite this dual use of two structure members.  An
alternative implementation would be to use a union, but use of a union is
against the coding standard (although an exception to the standard has been
permitted where the dual use also significantly changes the type of the
structure member). */
#define pxMutexHolder					pcTail
#define uxQueueType						pcHead
#define queueQUEUE_IS_MUTEX				NULL

/* Semaphores do not actually store or copy data, so have an item size of
zero. */
#define queueSEMAPHORE_QUEUE_ITEM_LENGTH ( ( unsigned portBASE_TYPE ) 0 )
#define queueMUTEX_GIVE_BLOCK_TIME		 ( ( portTickType ) 0U )

#if( configUSE_PREEMPTION == 0 )
/* If the cooperative scheduler is being used then a yield should not be
performed just because a higher priority task has been woken. */
#define queueYIELD_IF_USING_PREEMPTION()
#else
#define queueYIELD_IF_USING_PREEMPTION() portYIELD_WITHIN_API()
#endif

/*
 * Definition of the queue used by the scheduler.
 * Items are queued by copy, not reference.
 */
typedef struct QueueDefinition {
        signed char *pcHead;					/*< Points to the beginning of the queue storage area. */
        signed char *pcTail;					/*< Points to the byte at the end of the queue storage area.  Once more byte is allocated than necessary to store the queue items, this is used as a marker. */

        signed char *pcWriteTo;					/*< Points to the free next place in the storage area. */

        union {								/* Use of a union is an exception to the coding standard to ensure two mutually exclusive structure members don't appear simultaneously (wasting RAM). */
                signed char *pcReadFrom;			/*< Points to the last place that a queued item was read from when the structure is used as a queue. */
                unsigned portBASE_TYPE uxRecursiveCallCount;/*< Maintains a count of the numebr of times a recursive mutex has been recursively 'taken' when the structure is used as a mutex. */
        } u;

        xList xTasksWaitingToSend;				/*< List of tasks that are blocked waiting to post onto this queue.  Stored in priority order. */
        xList xTasksWaitingToReceive;			/*< List of tasks that are blocked waiting to read from this queue.  Stored in priority order. */

        volatile unsigned portBASE_TYPE uxMessagesWaiting;/*< The number of items currently in the queue. */
        unsigned portBASE_TYPE uxLength;		/*< The length of the queue defined as the number of items it will hold, not the number of bytes. */
        unsigned portBASE_TYPE uxItemSize;		/*< The size of each items that the queue will hold. */

        volatile signed portBASE_TYPE xRxLock;	/*< Stores the number of items received from the queue (removed from the queue) while the queue was locked.  Set to queueUNLOCKED when the queue is not locked. */
        volatile signed portBASE_TYPE xTxLock;	/*< Stores the number of items transmitted to the queue (added to the queue) while the queue was locked.  Set to queueUNLOCKED when the queue is not locked. */

#if ( configUSE_TRACE_FACILITY == 1 )
        unsigned char ucQueueNumber;
        unsigned char ucQueueType;
#endif

#if ( configUSE_QUEUE_SETS == 1 )
        struct QueueDefinition *pxQueueSetContainer;
#endif

} xQUEUE;
/*-----------------------------------------------------------*/

/*
 * The queue registry is just a means for kernel aware debuggers to locate
 * queue structures.  It has no other purpose so is an optional component.
 */
#if ( configQUEUE_REGISTRY_SIZE > 0 )

/* The type stored within the queue registry array.  This allows a name
to be assigned to each queue making kernel aware debugging a little
more user friendly. */
typedef struct QUEUE_REGISTRY_ITEM {
        signed char *pcQueueName;
        xQueueHandle xHandle;
} xQueueRegistryItem;

/* The queue registry is simply an array of xQueueRegistryItem structures.
The pcQueueName member of a structure being NULL is indicative of the
array position being vacant. */
xQueueRegistryItem xQueueRegistry[ configQUEUE_REGISTRY_SIZE ];

#endif /* configQUEUE_REGISTRY_SIZE */

/*
 * Unlocks a queue locked by a call to prvLockQueue.  Locking a queue does not
 * prevent an ISR from adding or removing items to the queue, but does prevent
 * an ISR from removing tasks from the queue event lists.  If an ISR finds a
 * queue is locked it will instead increment the appropriate queue lock count
 * to indicate that a task may require unblocking.  When the queue in unlocked
 * these lock counts are inspected, and the appropriate action taken.
 */
static void prvUnlockQueue( xQUEUE *pxQueue ) PRIVILEGED_FUNCTION;

/*
 * Uses a critical section to determine if there is any data in a queue.
 *
 * @return pdTRUE if the queue contains no items, otherwise pdFALSE.
 */
static signed portBASE_TYPE prvIsQueueEmpty( const xQUEUE *pxQueue ) PRIVILEGED_FUNCTION;

/*
 * Uses a critical section to determine if there is any space in a queue.
 *
 * @return pdTRUE if there is no space, otherwise pdFALSE;
 */
static signed portBASE_TYPE prvIsQueueFull( const xQUEUE *pxQueue ) PRIVILEGED_FUNCTION;

/*
 * Copies an item into the queue, either at the front of the queue or the
 * back of the queue.
 */
static void prvCopyDataToQueue( xQUEUE *pxQueue, const void *pvItemToQueue, portBASE_TYPE xPosition ) PRIVILEGED_FUNCTION;

/*
 * Copies an item out of a queue.
 */
static void prvCopyDataFromQueue( xQUEUE * const pxQueue, void * const pvBuffer ) PRIVILEGED_FUNCTION;

#if ( configUSE_QUEUE_SETS == 1 )
/*
 * Checks to see if a queue is a member of a queue set, and if so, notifies
 * the queue set that the queue contains data.
 */
static portBASE_TYPE prvNotifyQueueSetContainer( const xQUEUE * const pxQueue, portBASE_TYPE xCopyPosition ) PRIVILEGED_FUNCTION;
#endif

/*-----------------------------------------------------------*/

/*
 * Macro to mark a queue as locked.  Locking a queue prevents an ISR from
 * accessing the queue event lists.
 */
#define prvLockQueue( pxQueue )								\
	taskENTER_CRITICAL();									\
	{														\
		if( ( pxQueue )->xRxLock == queueUNLOCKED )			\
		{													\
			( pxQueue )->xRxLock = queueLOCKED_UNMODIFIED;	\
		}													\
		if( ( pxQueue )->xTxLock == queueUNLOCKED )			\
		{													\
			( pxQueue )->xTxLock = queueLOCKED_UNMODIFIED;	\
		}													\
	}														\
	taskEXIT_CRITICAL()
/*-----------------------------------------------------------*/

portBASE_TYPE xQueueGenericReset( xQueueHandle xQueue, portBASE_TYPE xNewQueue )
{
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );

        taskENTER_CRITICAL();
        {
                pxQueue->pcTail = pxQueue->pcHead + ( pxQueue->uxLength * pxQueue->uxItemSize );
                pxQueue->uxMessagesWaiting = ( unsigned portBASE_TYPE ) 0U;
                pxQueue->pcWriteTo = pxQueue->pcHead;
                pxQueue->u.pcReadFrom = pxQueue->pcHead + ( ( pxQueue->uxLength - ( unsigned portBASE_TYPE ) 1U ) * pxQueue->uxItemSize );
                pxQueue->xRxLock = queueUNLOCKED;
                pxQueue->xTxLock = queueUNLOCKED;

                if( xNewQueue == pdFALSE ) {
                        /* If there are tasks blocked waiting to read from the queue, then
                        the tasks will remain blocked as after this function exits the queue
                        will still be empty.  If there are tasks blocked waiting to write to
                        the queue, then one should be unblocked as after this function exits
                        it will be possible to write to it. */
                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE ) {
                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) == pdTRUE ) {
                                        queueYIELD_IF_USING_PREEMPTION();
                                }
                        }
                } else {
                        /* Ensure the event queues start in the correct state. */
                        vListInitialise( &( pxQueue->xTasksWaitingToSend ) );
                        vListInitialise( &( pxQueue->xTasksWaitingToReceive ) );
                }
        }
        taskEXIT_CRITICAL();

        /* A value is returned for calling semantic consistency with previous
        versions. */
        return pdPASS;
}
/*-----------------------------------------------------------*/

xQueueHandle xQueueGenericCreate( unsigned portBASE_TYPE uxQueueLength, unsigned portBASE_TYPE uxItemSize, unsigned char ucQueueType )
{
        xQUEUE *pxNewQueue;
        size_t xQueueSizeInBytes;
        xQueueHandle xReturn = NULL;

        /* Remove compiler warnings about unused parameters should
        configUSE_TRACE_FACILITY not be set to 1. */
        ( void ) ucQueueType;

        /* Allocate the new queue structure. */
        if( uxQueueLength > ( unsigned portBASE_TYPE ) 0 ) {
                pxNewQueue = ( xQUEUE * ) pvPortMalloc( sizeof( xQUEUE ) );
                if( pxNewQueue != NULL ) {
                        /* Create the list of pointers to queue items.  The queue is one byte
                        longer than asked for to make wrap checking easier/faster. */
                        xQueueSizeInBytes = ( size_t ) ( uxQueueLength * uxItemSize ) + ( size_t ) 1; /*lint !e961 MISRA exception as the casts are only redundant for some ports. */

                        pxNewQueue->pcHead = ( signed char * ) pvPortMalloc( xQueueSizeInBytes );
                        if( pxNewQueue->pcHead != NULL ) {
                                /* Initialise the queue members as described above where the
                                queue type is defined. */
                                pxNewQueue->uxLength = uxQueueLength;
                                pxNewQueue->uxItemSize = uxItemSize;
                                ( void ) xQueueGenericReset( pxNewQueue, pdTRUE );

#if ( configUSE_TRACE_FACILITY == 1 )
                                {
                                        pxNewQueue->ucQueueType = ucQueueType;
                                }
#endif /* configUSE_TRACE_FACILITY */

#if( configUSE_QUEUE_SETS == 1 )
                                {
                                        pxNewQueue->pxQueueSetContainer = NULL;
                                }
#endif /* configUSE_QUEUE_SETS */

                                traceQUEUE_CREATE( pxNewQueue );
                                xReturn = pxNewQueue;
                        } else {
                                traceQUEUE_CREATE_FAILED( ucQueueType );
                                vPortFree( pxNewQueue );
                        }
                }
        }

        configASSERT( xReturn );

        return xReturn;
}
/*-----------------------------------------------------------*/

#if ( configUSE_MUTEXES == 1 )

xQueueHandle xQueueCreateMutex( unsigned char ucQueueType )
{
        xQUEUE *pxNewQueue;

        /* Prevent compiler warnings about unused parameters if
        configUSE_TRACE_FACILITY does not equal 1. */
        ( void ) ucQueueType;

        /* Allocate the new queue structure. */
        pxNewQueue = ( xQUEUE * ) pvPortMalloc( sizeof( xQUEUE ) );
        if( pxNewQueue != NULL ) {
                /* Information required for priority inheritance. */
                pxNewQueue->pxMutexHolder = NULL;
                pxNewQueue->uxQueueType = queueQUEUE_IS_MUTEX;

                /* Queues used as a mutex no data is actually copied into or out
                of the queue. */
                pxNewQueue->pcWriteTo = NULL;
                pxNewQueue->u.pcReadFrom = NULL;

                /* Each mutex has a length of 1 (like a binary semaphore) and
                an item size of 0 as nothing is actually copied into or out
                of the mutex. */
                pxNewQueue->uxMessagesWaiting = ( unsigned portBASE_TYPE ) 0U;
                pxNewQueue->uxLength = ( unsigned portBASE_TYPE ) 1U;
                pxNewQueue->uxItemSize = ( unsigned portBASE_TYPE ) 0U;
                pxNewQueue->xRxLock = queueUNLOCKED;
                pxNewQueue->xTxLock = queueUNLOCKED;

#if ( configUSE_TRACE_FACILITY == 1 )
                {
                        pxNewQueue->ucQueueType = ucQueueType;
                }
#endif

#if ( configUSE_QUEUE_SETS == 1 )
                {
                        pxNewQueue->pxQueueSetContainer = NULL;
                }
#endif

                /* Ensure the event queues start with the correct state. */
                vListInitialise( &( pxNewQueue->xTasksWaitingToSend ) );
                vListInitialise( &( pxNewQueue->xTasksWaitingToReceive ) );

                traceCREATE_MUTEX( pxNewQueue );

                /* Start with the semaphore in the expected state. */
                ( void ) xQueueGenericSend( pxNewQueue, NULL, ( portTickType ) 0U, queueSEND_TO_BACK );
        } else {
                traceCREATE_MUTEX_FAILED();
        }

        configASSERT( pxNewQueue );
        return pxNewQueue;
}

#endif /* configUSE_MUTEXES */
/*-----------------------------------------------------------*/

#if ( ( configUSE_MUTEXES == 1 ) && ( INCLUDE_xSemaphoreGetMutexHolder == 1 ) )

void* xQueueGetMutexHolder( xQueueHandle xSemaphore )
{
        void *pxReturn;

        /* This function is called by xSemaphoreGetMutexHolder(), and should not
        be called directly.  Note:  This is is a good way of determining if the
        calling task is the mutex holder, but not a good way of determining the
        identity of the mutex holder, as the holder may change between the
        following critical section exiting and the function returning. */
        taskENTER_CRITICAL();
        {
                if( ( ( xQUEUE * ) xSemaphore )->uxQueueType == queueQUEUE_IS_MUTEX ) {
                        pxReturn = ( void * ) ( ( xQUEUE * ) xSemaphore )->pxMutexHolder;
                } else {
                        pxReturn = NULL;
                }
        }
        taskEXIT_CRITICAL();

        return pxReturn;
}

#endif
/*-----------------------------------------------------------*/

#if ( configUSE_RECURSIVE_MUTEXES == 1 )

portBASE_TYPE xQueueGiveMutexRecursive( xQueueHandle xMutex )
{
        portBASE_TYPE xReturn;
        xQUEUE * const pxMutex = ( xQUEUE * ) xMutex;

        configASSERT( pxMutex );

        /* If this is the task that holds the mutex then pxMutexHolder will not
        change outside of this task.  If this task does not hold the mutex then
        pxMutexHolder can never coincidentally equal the tasks handle, and as
        this is the only condition we are interested in it does not matter if
        pxMutexHolder is accessed simultaneously by another task.  Therefore no
        mutual exclusion is required to test the pxMutexHolder variable. */
        if( pxMutex->pxMutexHolder == ( void * ) xTaskGetCurrentTaskHandle() ) { /*lint !e961 Not a redundant cast as xTaskHandle is a typedef. */
                traceGIVE_MUTEX_RECURSIVE( pxMutex );

                /* uxRecursiveCallCount cannot be zero if pxMutexHolder is equal to
                the task handle, therefore no underflow check is required.  Also,
                uxRecursiveCallCount is only modified by the mutex holder, and as
                there can only be one, no mutual exclusion is required to modify the
                uxRecursiveCallCount member. */
                ( pxMutex->u.uxRecursiveCallCount )--;

                /* Have we unwound the call count? */
                if( pxMutex->u.uxRecursiveCallCount == ( unsigned portBASE_TYPE ) 0 ) {
                        /* Return the mutex.  This will automatically unblock any other
                        task that might be waiting to access the mutex. */
                        ( void ) xQueueGenericSend( pxMutex, NULL, queueMUTEX_GIVE_BLOCK_TIME, queueSEND_TO_BACK );
                }

                xReturn = pdPASS;
        } else {
                /* We cannot give the mutex because we are not the holder. */
                xReturn = pdFAIL;

                traceGIVE_MUTEX_RECURSIVE_FAILED( pxMutex );
        }

        return xReturn;
}

#endif /* configUSE_RECURSIVE_MUTEXES */
/*-----------------------------------------------------------*/

#if ( configUSE_RECURSIVE_MUTEXES == 1 )

portBASE_TYPE xQueueTakeMutexRecursive( xQueueHandle xMutex, portTickType xBlockTime )
{
        portBASE_TYPE xReturn;
        xQUEUE * const pxMutex = ( xQUEUE * ) xMutex;

        configASSERT( pxMutex );

        /* Comments regarding mutual exclusion as per those within
        xQueueGiveMutexRecursive(). */

        traceTAKE_MUTEX_RECURSIVE( pxMutex );

        if( pxMutex->pxMutexHolder == ( void * ) xTaskGetCurrentTaskHandle() ) { /*lint !e961 Cast is not redundant as xTaskHandle is a typedef. */
                ( pxMutex->u.uxRecursiveCallCount )++;
                xReturn = pdPASS;
        } else {
                xReturn = xQueueGenericReceive( pxMutex, NULL, xBlockTime, pdFALSE );

                /* pdPASS will only be returned if we successfully obtained the mutex,
                we may have blocked to reach here. */
                if( xReturn == pdPASS ) {
                        ( pxMutex->u.uxRecursiveCallCount )++;
                } else {
                        traceTAKE_MUTEX_RECURSIVE_FAILED( pxMutex );
                }
        }

        return xReturn;
}

#endif /* configUSE_RECURSIVE_MUTEXES */
/*-----------------------------------------------------------*/

#if ( configUSE_COUNTING_SEMAPHORES == 1 )

xQueueHandle xQueueCreateCountingSemaphore( unsigned portBASE_TYPE uxMaxCount, unsigned portBASE_TYPE uxInitialCount )
{
        xQueueHandle xHandle;

        configASSERT( uxMaxCount != 0 );
        configASSERT( uxInitialCount <= uxMaxCount );

        xHandle = xQueueGenericCreate( uxMaxCount, queueSEMAPHORE_QUEUE_ITEM_LENGTH, queueQUEUE_TYPE_COUNTING_SEMAPHORE );

        if( xHandle != NULL ) {
                ( ( xQUEUE * ) xHandle )->uxMessagesWaiting = uxInitialCount;

                traceCREATE_COUNTING_SEMAPHORE();
        } else {
                traceCREATE_COUNTING_SEMAPHORE_FAILED();
        }

        configASSERT( xHandle );
        return xHandle;
}

#endif /* configUSE_COUNTING_SEMAPHORES */
/*-----------------------------------------------------------*/

signed portBASE_TYPE xQueueGenericSend( xQueueHandle xQueue, const void * const pvItemToQueue, portTickType xTicksToWait, portBASE_TYPE xCopyPosition )
{
        signed portBASE_TYPE xEntryTimeSet = pdFALSE;
        xTimeOutType xTimeOut;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );
        configASSERT( !( ( pvItemToQueue == NULL ) && ( pxQueue->uxItemSize != ( unsigned portBASE_TYPE ) 0U ) ) );
        configASSERT( !( ( xCopyPosition == queueOVERWRITE ) && ( pxQueue->uxLength != 1 ) ) );

        /* This function relaxes the coding standard somewhat to allow return
        statements within the function itself.  This is done in the interest
        of execution time efficiency. */
        for( ;; ) {
                taskENTER_CRITICAL();
                {
                        /* Is there room on the queue now?  The running task must be
                        the highest priority task wanting to access the queue.  If
                        the head item in the queue is to be overwritten then it does
                        not matter if the queue is full. */
                        if( ( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) || ( xCopyPosition == queueOVERWRITE ) ) {
                                traceQUEUE_SEND( pxQueue );
                                prvCopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );

#if ( configUSE_QUEUE_SETS == 1 )
                                {
                                        if( pxQueue->pxQueueSetContainer != NULL ) {
                                                if( prvNotifyQueueSetContainer( pxQueue, xCopyPosition ) == pdTRUE ) {
                                                        /* The queue is a member of a queue set, and posting
                                                        to the queue set caused a higher priority task to
                                                        unblock. A context switch is required. */
                                                        queueYIELD_IF_USING_PREEMPTION();
                                                }
                                        } else {
                                                /* If there was a task waiting for data to arrive on the
                                                queue then unblock it now. */
                                                if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) == pdTRUE ) {
                                                                /* The unblocked task has a priority higher than
                                                                our own so yield immediately.  Yes it is ok to
                                                                do this from within the critical section - the
                                                                kernel takes care of that. */
                                                                queueYIELD_IF_USING_PREEMPTION();
                                                        }
                                                }
                                        }
                                }
#else /* configUSE_QUEUE_SETS */
                                {
                                        /* If there was a task waiting for data to arrive on the
                                        queue then unblock it now. */
                                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) == pdTRUE ) {
                                                        /* The unblocked task has a priority higher than
                                                        our own so yield immediately.  Yes it is ok to do
                                                        this from within the critical section - the kernel
                                                        takes care of that. */
                                                        queueYIELD_IF_USING_PREEMPTION();
                                                }
                                        }
                                }
#endif /* configUSE_QUEUE_SETS */

                                taskEXIT_CRITICAL();

                                /* Return to the original privilege level before exiting the
                                function. */
                                return pdPASS;
                        } else {
                                if( xTicksToWait == ( portTickType ) 0 ) {
                                        /* The queue was full and no block time is specified (or
                                        the block time has expired) so leave now. */
                                        taskEXIT_CRITICAL();

                                        /* Return to the original privilege level before exiting
                                        the function. */
                                        traceQUEUE_SEND_FAILED( pxQueue );
                                        return errQUEUE_FULL;
                                } else if( xEntryTimeSet == pdFALSE ) {
                                        /* The queue was full and a block time was specified so
                                        configure the timeout structure. */
                                        vTaskSetTimeOutState( &xTimeOut );
                                        xEntryTimeSet = pdTRUE;
                                } else {
                                        /* Entry time was already set. */
                                }
                        }
                }
                taskEXIT_CRITICAL();

                /* Interrupts and other tasks can send to and receive from the queue
                now the critical section has been exited. */

                vTaskSuspendAll();
                prvLockQueue( pxQueue );

                /* Update the timeout state to see if it has expired yet. */
                if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE ) {
                        if( prvIsQueueFull( pxQueue ) != pdFALSE ) {
                                traceBLOCKING_ON_QUEUE_SEND( pxQueue );
                                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToSend ), xTicksToWait );

                                /* Unlocking the queue means queue events can effect the
                                event list.  It is possible	that interrupts occurring now
                                remove this task from the event	list again - but as the
                                scheduler is suspended the task will go onto the pending
                                ready last instead of the actual ready list. */
                                prvUnlockQueue( pxQueue );

                                /* Resuming the scheduler will move tasks from the pending
                                ready list into the ready list - so it is feasible that this
                                task is already in a ready list before it yields - in which
                                case the yield will not cause a context switch unless there
                                is also a higher priority task in the pending ready list. */
                                if( xTaskResumeAll() == pdFALSE ) {
                                        portYIELD_WITHIN_API();
                                }
                        } else {
                                /* Try again. */
                                prvUnlockQueue( pxQueue );
                                ( void ) xTaskResumeAll();
                        }
                } else {
                        /* The timeout has expired. */
                        prvUnlockQueue( pxQueue );
                        ( void ) xTaskResumeAll();

                        /* Return to the original privilege level before exiting the
                        function. */
                        traceQUEUE_SEND_FAILED( pxQueue );
                        return errQUEUE_FULL;
                }
        }
}
/*-----------------------------------------------------------*/

#if ( configUSE_ALTERNATIVE_API == 1 )

signed portBASE_TYPE xQueueAltGenericSend( xQueueHandle xQueue, const void * const pvItemToQueue, portTickType xTicksToWait, portBASE_TYPE xCopyPosition )
{
        signed portBASE_TYPE xEntryTimeSet = pdFALSE;
        xTimeOutType xTimeOut;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );
        configASSERT( !( ( pvItemToQueue == NULL ) && ( pxQueue->uxItemSize != ( unsigned portBASE_TYPE ) 0U ) ) );

        for( ;; ) {
                taskENTER_CRITICAL();
                {
                        /* Is there room on the queue now?  To be running we must be
                        the highest priority task wanting to access the queue. */
                        if( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) {
                                traceQUEUE_SEND( pxQueue );
                                prvCopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );

                                /* If there was a task waiting for data to arrive on the
                                queue then unblock it now. */
                                if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) == pdTRUE ) {
                                                /* The unblocked task has a priority higher than
                                                our own so yield immediately. */
                                                portYIELD_WITHIN_API();
                                        }
                                }

                                taskEXIT_CRITICAL();
                                return pdPASS;
                        } else {
                                if( xTicksToWait == ( portTickType ) 0 ) {
                                        taskEXIT_CRITICAL();
                                        return errQUEUE_FULL;
                                } else if( xEntryTimeSet == pdFALSE ) {
                                        vTaskSetTimeOutState( &xTimeOut );
                                        xEntryTimeSet = pdTRUE;
                                }
                        }
                }
                taskEXIT_CRITICAL();

                taskENTER_CRITICAL();
                {
                        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE ) {
                                if( prvIsQueueFull( pxQueue ) != pdFALSE ) {
                                        traceBLOCKING_ON_QUEUE_SEND( pxQueue );
                                        vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToSend ), xTicksToWait );
                                        portYIELD_WITHIN_API();
                                }
                        } else {
                                taskEXIT_CRITICAL();
                                traceQUEUE_SEND_FAILED( pxQueue );
                                return errQUEUE_FULL;
                        }
                }
                taskEXIT_CRITICAL();
        }
}

#endif /* configUSE_ALTERNATIVE_API */
/*-----------------------------------------------------------*/

#if ( configUSE_ALTERNATIVE_API == 1 )

signed portBASE_TYPE xQueueAltGenericReceive( xQueueHandle xQueue, void * const pvBuffer, portTickType xTicksToWait, portBASE_TYPE xJustPeeking )
{
        signed portBASE_TYPE xEntryTimeSet = pdFALSE;
        xTimeOutType xTimeOut;
        signed char *pcOriginalReadPosition;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );
        configASSERT( !( ( pvBuffer == NULL ) && ( pxQueue->uxItemSize != ( unsigned portBASE_TYPE ) 0U ) ) );

        for( ;; ) {
                taskENTER_CRITICAL();
                {
                        if( pxQueue->uxMessagesWaiting > ( unsigned portBASE_TYPE ) 0 ) {
                                /* Remember our read position in case we are just peeking. */
                                pcOriginalReadPosition = pxQueue->u.pcReadFrom;

                                prvCopyDataFromQueue( pxQueue, pvBuffer );

                                if( xJustPeeking == pdFALSE ) {
                                        traceQUEUE_RECEIVE( pxQueue );

                                        /* Data is actually being removed (not just peeked). */
                                        --( pxQueue->uxMessagesWaiting );

#if ( configUSE_MUTEXES == 1 )
                                        {
                                                if( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX ) {
                                                        /* Record the information required to implement
                                                        priority inheritance should it become necessary. */
                                                        pxQueue->pxMutexHolder = ( signed char * ) xTaskGetCurrentTaskHandle();
                                                }
                                        }
#endif

                                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE ) {
                                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) == pdTRUE ) {
                                                        portYIELD_WITHIN_API();
                                                }
                                        }
                                } else {
                                        traceQUEUE_PEEK( pxQueue );

                                        /* We are not removing the data, so reset our read
                                        pointer. */
                                        pxQueue->u.pcReadFrom = pcOriginalReadPosition;

                                        /* The data is being left in the queue, so see if there are
                                        any other tasks waiting for the data. */
                                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                                /* Tasks that are removed from the event list will get added to
                                                the pending ready list as the scheduler is still suspended. */
                                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                                        /* The task waiting has a higher priority than this task. */
                                                        portYIELD_WITHIN_API();
                                                }
                                        }

                                }

                                taskEXIT_CRITICAL();
                                return pdPASS;
                        } else {
                                if( xTicksToWait == ( portTickType ) 0 ) {
                                        taskEXIT_CRITICAL();
                                        traceQUEUE_RECEIVE_FAILED( pxQueue );
                                        return errQUEUE_EMPTY;
                                } else if( xEntryTimeSet == pdFALSE ) {
                                        vTaskSetTimeOutState( &xTimeOut );
                                        xEntryTimeSet = pdTRUE;
                                }
                        }
                }
                taskEXIT_CRITICAL();

                taskENTER_CRITICAL();
                {
                        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE ) {
                                if( prvIsQueueEmpty( pxQueue ) != pdFALSE ) {
                                        traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue );

#if ( configUSE_MUTEXES == 1 )
                                        {
                                                if( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX ) {
                                                        portENTER_CRITICAL();
                                                        {
                                                                vTaskPriorityInherit( ( void * ) pxQueue->pxMutexHolder );
                                                        }
                                                        portEXIT_CRITICAL();
                                                }
                                        }
#endif

                                        vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait );
                                        portYIELD_WITHIN_API();
                                }
                        } else {
                                taskEXIT_CRITICAL();
                                traceQUEUE_RECEIVE_FAILED( pxQueue );
                                return errQUEUE_EMPTY;
                        }
                }
                taskEXIT_CRITICAL();
        }
}


#endif /* configUSE_ALTERNATIVE_API */
/*-----------------------------------------------------------*/

signed portBASE_TYPE xQueueGenericSendFromISR( xQueueHandle xQueue, const void * const pvItemToQueue, signed portBASE_TYPE *pxHigherPriorityTaskWoken, portBASE_TYPE xCopyPosition )
{
        signed portBASE_TYPE xReturn;
        unsigned portBASE_TYPE uxSavedInterruptStatus;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );
        configASSERT( !( ( pvItemToQueue == NULL ) && ( pxQueue->uxItemSize != ( unsigned portBASE_TYPE ) 0U ) ) );
        configASSERT( !( ( xCopyPosition == queueOVERWRITE ) && ( pxQueue->uxLength != 1 ) ) );

        /* RTOS ports that support interrupt nesting have the concept of a maximum
        system call (or maximum API call) interrupt priority.  Interrupts that are
        above the maximum system call priority are keep permanently enabled, even
        when the RTOS kernel is in a critical section, but cannot make any calls to
        FreeRTOS API functions.  If configASSERT() is defined in FreeRTOSConfig.h
        then portASSERT_IF_INTERRUPT_PRIORITY_INVALID() will result in an assertion
        failure if a FreeRTOS API function is called from an interrupt that has been
        assigned a priority above the configured maximum system call priority.
        Only FreeRTOS functions that end in FromISR can be called from interrupts
        that have been assigned a priority at or (logically) below the maximum
        system call	interrupt priority.  FreeRTOS maintains a separate interrupt
        safe API to ensure interrupt entry is as fast and as simple as possible.
        More information (albeit Cortex-M specific) is provided on the following
        link: http://www.freertos.org/RTOS-Cortex-M3-M4.html */
//	portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

        /* Similar to xQueueGenericSend, except we don't block if there is no room
        in the queue.  Also we don't directly wake a task that was blocked on a
        queue read, instead we return a flag to say whether a context switch is
        required or not (i.e. has a task with a higher priority than us been woken
        by this	post). */
        uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();
        {
                if( ( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) || ( xCopyPosition == queueOVERWRITE ) ) {
                        traceQUEUE_SEND_FROM_ISR( pxQueue );

                        prvCopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );

                        /* If the queue is locked we do not alter the event list.  This will
                        be done when the queue is unlocked later. */
                        if( pxQueue->xTxLock == queueUNLOCKED ) {
#if ( configUSE_QUEUE_SETS == 1 )
                                {
                                        if( pxQueue->pxQueueSetContainer != NULL )
                                        {
                                                if( prvNotifyQueueSetContainer( pxQueue, xCopyPosition ) == pdTRUE ) {
                                                        /* The queue is a member of a queue set, and posting
                                                        to the queue set caused a higher priority task to
                                                        unblock.  A context switch is required. */
                                                        if( pxHigherPriorityTaskWoken != NULL ) {
                                                                *pxHigherPriorityTaskWoken = pdTRUE;
                                                        }
                                                }
                                        } else
                                        {
                                                if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                                                /* The task waiting has a higher priority so record that a
                                                                context	switch is required. */
                                                                if( pxHigherPriorityTaskWoken != NULL ) {
                                                                        *pxHigherPriorityTaskWoken = pdTRUE;
                                                                }
                                                        }
                                                }
                                        }
                                }
#else /* configUSE_QUEUE_SETS */
                                {
                                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE )
                                        {
                                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                                        /* The task waiting has a higher priority so record that a
                                                        context	switch is required. */
                                                        if( pxHigherPriorityTaskWoken != NULL ) {
                                                                *pxHigherPriorityTaskWoken = pdTRUE;
                                                        }
                                                }
                                        }
                                }
#endif /* configUSE_QUEUE_SETS */
                        } else {
                                /* Increment the lock count so the task that unlocks the queue
                                knows that data was posted while it was locked. */
                                ++( pxQueue->xTxLock );
                        }

                        xReturn = pdPASS;
                } else {
                        traceQUEUE_SEND_FROM_ISR_FAILED( pxQueue );
                        xReturn = errQUEUE_FULL;
                }
        }
        portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );

        return xReturn;
}
/*-----------------------------------------------------------*/

signed portBASE_TYPE xQueueGenericReceive( xQueueHandle xQueue, void * const pvBuffer, portTickType xTicksToWait, portBASE_TYPE xJustPeeking )
{
        signed portBASE_TYPE xEntryTimeSet = pdFALSE;
        xTimeOutType xTimeOut;
        signed char *pcOriginalReadPosition;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );
        configASSERT( !( ( pvBuffer == NULL ) && ( pxQueue->uxItemSize != ( unsigned portBASE_TYPE ) 0U ) ) );

        /* This function relaxes the coding standard somewhat to allow return
        statements within the function itself.  This is done in the interest
        of execution time efficiency. */

        for( ;; ) {
                taskENTER_CRITICAL();
                {
                        /* Is there data in the queue now?  To be running we must be
                        the highest priority task wanting to access the queue. */
                        if( pxQueue->uxMessagesWaiting > ( unsigned portBASE_TYPE ) 0 ) {
                                /* Remember the read position in case the queue is only being
                                peeked. */
                                pcOriginalReadPosition = pxQueue->u.pcReadFrom;

                                prvCopyDataFromQueue( pxQueue, pvBuffer );

                                if( xJustPeeking == pdFALSE ) {
                                        traceQUEUE_RECEIVE( pxQueue );

                                        /* Actually removing data, not just peeking. */
                                        --( pxQueue->uxMessagesWaiting );

#if ( configUSE_MUTEXES == 1 )
                                        {
                                                if( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX ) {
                                                        /* Record the information required to implement
                                                        priority inheritance should it become necessary. */
                                                        pxQueue->pxMutexHolder = ( signed char * ) xTaskGetCurrentTaskHandle(); /*lint !e961 Cast is not redundant as xTaskHandle is a typedef. */
                                                }
                                        }
#endif

                                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE ) {
                                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) == pdTRUE ) {
                                                        queueYIELD_IF_USING_PREEMPTION();
                                                }
                                        }
                                } else {
                                        traceQUEUE_PEEK( pxQueue );

                                        /* The data is not being removed, so reset the read
                                        pointer. */
                                        pxQueue->u.pcReadFrom = pcOriginalReadPosition;

                                        /* The data is being left in the queue, so see if there are
                                        any other tasks waiting for the data. */
                                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                                /* Tasks that are removed from the event list will get added to
                                                the pending ready list as the scheduler is still suspended. */
                                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                                        /* The task waiting has a higher priority than this task. */
                                                        queueYIELD_IF_USING_PREEMPTION();
                                                }
                                        }
                                }

                                taskEXIT_CRITICAL();
                                return pdPASS;
                        } else {
                                if( xTicksToWait == ( portTickType ) 0 ) {
                                        /* The queue was empty and no block time is specified (or
                                        the block time has expired) so leave now. */
                                        taskEXIT_CRITICAL();
                                        traceQUEUE_RECEIVE_FAILED( pxQueue );
                                        return errQUEUE_EMPTY;
                                } else if( xEntryTimeSet == pdFALSE ) {
                                        /* The queue was empty and a block time was specified so
                                        configure the timeout structure. */
                                        vTaskSetTimeOutState( &xTimeOut );
                                        xEntryTimeSet = pdTRUE;
                                } else {
                                        /* Entry time was already set. */
                                }
                        }
                }
                taskEXIT_CRITICAL();

                /* Interrupts and other tasks can send to and receive from the queue
                now the critical section has been exited. */

                vTaskSuspendAll();
                prvLockQueue( pxQueue );

                /* Update the timeout state to see if it has expired yet. */
                if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE ) {
                        if( prvIsQueueEmpty( pxQueue ) != pdFALSE ) {
                                traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue );

#if ( configUSE_MUTEXES == 1 )
                                {
                                        if( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX ) {
                                                portENTER_CRITICAL();
                                                {
                                                        vTaskPriorityInherit( ( void * ) pxQueue->pxMutexHolder );
                                                }
                                                portEXIT_CRITICAL();
                                        }
                                }
#endif

                                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait );
                                prvUnlockQueue( pxQueue );
                                if( xTaskResumeAll() == pdFALSE ) {
                                        portYIELD_WITHIN_API();
                                }
                        } else {
                                /* Try again. */
                                prvUnlockQueue( pxQueue );
                                ( void ) xTaskResumeAll();
                        }
                } else {
                        prvUnlockQueue( pxQueue );
                        ( void ) xTaskResumeAll();
                        traceQUEUE_RECEIVE_FAILED( pxQueue );
                        return errQUEUE_EMPTY;
                }
        }
}
/*-----------------------------------------------------------*/

signed portBASE_TYPE xQueueReceiveFromISR( xQueueHandle xQueue, void * const pvBuffer, signed portBASE_TYPE *pxHigherPriorityTaskWoken )
{
        signed portBASE_TYPE xReturn;
        unsigned portBASE_TYPE uxSavedInterruptStatus;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );
        configASSERT( !( ( pvBuffer == NULL ) && ( pxQueue->uxItemSize != ( unsigned portBASE_TYPE ) 0U ) ) );

        /* RTOS ports that support interrupt nesting have the concept of a maximum
        system call (or maximum API call) interrupt priority.  Interrupts that are
        above the maximum system call priority are keep permanently enabled, even
        when the RTOS kernel is in a critical section, but cannot make any calls to
        FreeRTOS API functions.  If configASSERT() is defined in FreeRTOSConfig.h
        then portASSERT_IF_INTERRUPT_PRIORITY_INVALID() will result in an assertion
        failure if a FreeRTOS API function is called from an interrupt that has been
        assigned a priority above the configured maximum system call priority.
        Only FreeRTOS functions that end in FromISR can be called from interrupts
        that have been assigned a priority at or (logically) below the maximum
        system call	interrupt priority.  FreeRTOS maintains a separate interrupt
        safe API to ensure interrupt entry is as fast and as simple as possible.
        More information (albeit Cortex-M specific) is provided on the following
        link: http://www.freertos.org/RTOS-Cortex-M3-M4.html */
//	portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

        uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();
        {
                /* Cannot block in an ISR, so check there is data available. */
                if( pxQueue->uxMessagesWaiting > ( unsigned portBASE_TYPE ) 0 ) {
                        traceQUEUE_RECEIVE_FROM_ISR( pxQueue );

                        prvCopyDataFromQueue( pxQueue, pvBuffer );
                        --( pxQueue->uxMessagesWaiting );

                        /* If the queue is locked the event list will not be modified.
                        Instead update the lock count so the task that unlocks the queue
                        will know that an ISR has removed data while the queue was
                        locked. */
                        if( pxQueue->xRxLock == queueUNLOCKED ) {
                                if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE ) {
                                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != pdFALSE ) {
                                                /* The task waiting has a higher priority than us so
                                                force a context switch. */
                                                if( pxHigherPriorityTaskWoken != NULL ) {
                                                        *pxHigherPriorityTaskWoken = pdTRUE;
                                                }
                                        }
                                }
                        } else {
                                /* Increment the lock count so the task that unlocks the queue
                                knows that data was removed while it was locked. */
                                ++( pxQueue->xRxLock );
                        }

                        xReturn = pdPASS;
                } else {
                        xReturn = pdFAIL;
                        traceQUEUE_RECEIVE_FROM_ISR_FAILED( pxQueue );
                }
        }
        portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );

        return xReturn;
}
/*-----------------------------------------------------------*/

signed portBASE_TYPE xQueuePeekFromISR( xQueueHandle xQueue,  void * const pvBuffer )
{
        signed portBASE_TYPE xReturn;
        unsigned portBASE_TYPE uxSavedInterruptStatus;
        signed char *pcOriginalReadPosition;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );
        configASSERT( !( ( pvBuffer == NULL ) && ( pxQueue->uxItemSize != ( unsigned portBASE_TYPE ) 0U ) ) );

        /* RTOS ports that support interrupt nesting have the concept of a maximum
        system call (or maximum API call) interrupt priority.  Interrupts that are
        above the maximum system call priority are keep permanently enabled, even
        when the RTOS kernel is in a critical section, but cannot make any calls to
        FreeRTOS API functions.  If configASSERT() is defined in FreeRTOSConfig.h
        then portASSERT_IF_INTERRUPT_PRIORITY_INVALID() will result in an assertion
        failure if a FreeRTOS API function is called from an interrupt that has been
        assigned a priority above the configured maximum system call priority.
        Only FreeRTOS functions that end in FromISR can be called from interrupts
        that have been assigned a priority at or (logically) below the maximum
        system call	interrupt priority.  FreeRTOS maintains a separate interrupt
        safe API to ensure interrupt entry is as fast and as simple as possible.
        More information (albeit Cortex-M specific) is provided on the following
        link: http://www.freertos.org/RTOS-Cortex-M3-M4.html */
        portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

        uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();
        {
                /* Cannot block in an ISR, so check there is data available. */
                if( pxQueue->uxMessagesWaiting > ( unsigned portBASE_TYPE ) 0 ) {
                        traceQUEUE_PEEK_FROM_ISR( pxQueue );

                        /* Remember the read position so it can be reset as nothing is
                        actually being removed from the queue. */
                        pcOriginalReadPosition = pxQueue->u.pcReadFrom;
                        prvCopyDataFromQueue( pxQueue, pvBuffer );
                        pxQueue->u.pcReadFrom = pcOriginalReadPosition;

                        xReturn = pdPASS;
                } else {
                        xReturn = pdFAIL;
                        traceQUEUE_PEEK_FROM_ISR_FAILED( pxQueue );
                }
        }
        portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );

        return xReturn;
}
/*-----------------------------------------------------------*/

unsigned portBASE_TYPE uxQueueMessagesWaiting( const xQueueHandle xQueue )
{
        unsigned portBASE_TYPE uxReturn;

        configASSERT( xQueue );

        taskENTER_CRITICAL();
        uxReturn = ( ( xQUEUE * ) xQueue )->uxMessagesWaiting;
        taskEXIT_CRITICAL();

        return uxReturn;
} /*lint !e818 Pointer cannot be declared const as xQueue is a typedef not pointer. */
/*-----------------------------------------------------------*/

unsigned portBASE_TYPE uxQueueSpacesAvailable( const xQueueHandle xQueue )
{
        unsigned portBASE_TYPE uxReturn;
        xQUEUE *pxQueue;

        pxQueue = ( xQUEUE * ) xQueue;
        configASSERT( pxQueue );

        taskENTER_CRITICAL();
        uxReturn = pxQueue->uxLength - pxQueue->uxMessagesWaiting;
        taskEXIT_CRITICAL();

        return uxReturn;
} /*lint !e818 Pointer cannot be declared const as xQueue is a typedef not pointer. */
/*-----------------------------------------------------------*/

unsigned portBASE_TYPE uxQueueMessagesWaitingFromISR( const xQueueHandle xQueue )
{
        unsigned portBASE_TYPE uxReturn;

        configASSERT( xQueue );

        uxReturn = ( ( xQUEUE * ) xQueue )->uxMessagesWaiting;

        return uxReturn;
} /*lint !e818 Pointer cannot be declared const as xQueue is a typedef not pointer. */
/*-----------------------------------------------------------*/

void vQueueDelete( xQueueHandle xQueue )
{
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        configASSERT( pxQueue );

        traceQUEUE_DELETE( pxQueue );
#if ( configQUEUE_REGISTRY_SIZE > 0 )
        {
                vQueueUnregisterQueue( pxQueue );
        }
#endif
        vPortFree( pxQueue->pcHead );
        vPortFree( pxQueue );
}
/*-----------------------------------------------------------*/

#if ( configUSE_TRACE_FACILITY == 1 )

unsigned char ucQueueGetQueueNumber( xQueueHandle xQueue )
{
        return ( ( xQUEUE * ) xQueue )->ucQueueNumber;
}

#endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

#if ( configUSE_TRACE_FACILITY == 1 )

void vQueueSetQueueNumber( xQueueHandle xQueue, unsigned char ucQueueNumber )
{
        ( ( xQUEUE * ) xQueue )->ucQueueNumber = ucQueueNumber;
}

#endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

#if ( configUSE_TRACE_FACILITY == 1 )

unsigned char ucQueueGetQueueType( xQueueHandle xQueue )
{
        return ( ( xQUEUE * ) xQueue )->ucQueueType;
}

#endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

static void prvCopyDataToQueue( xQUEUE *pxQueue, const void *pvItemToQueue, portBASE_TYPE xPosition )
{
        if( pxQueue->uxItemSize == ( unsigned portBASE_TYPE ) 0 ) {
#if ( configUSE_MUTEXES == 1 )
                {
                        if( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX )
                        {
                                /* The mutex is no longer being held. */
                                vTaskPriorityDisinherit( ( void * ) pxQueue->pxMutexHolder );
                                pxQueue->pxMutexHolder = NULL;
                        }
                }
#endif /* configUSE_MUTEXES */
        } else if( xPosition == queueSEND_TO_BACK ) {
                ( void ) memcpy( ( void * ) pxQueue->pcWriteTo, pvItemToQueue, ( size_t ) pxQueue->uxItemSize ); /*lint !e961 !e418 MISRA exception as the casts are only redundant for some ports, plus previous logic ensures a null pointer can only be passed to memcpy() if the copy size is 0. */
                pxQueue->pcWriteTo += pxQueue->uxItemSize;
                if( pxQueue->pcWriteTo >= pxQueue->pcTail ) { /*lint !e946 MISRA exception justified as comparison of pointers is the cleanest solution. */
                        pxQueue->pcWriteTo = pxQueue->pcHead;
                }
        } else {
                ( void ) memcpy( ( void * ) pxQueue->u.pcReadFrom, pvItemToQueue, ( size_t ) pxQueue->uxItemSize ); /*lint !e961 MISRA exception as the casts are only redundant for some ports. */
                pxQueue->u.pcReadFrom -= pxQueue->uxItemSize;
                if( pxQueue->u.pcReadFrom < pxQueue->pcHead ) { /*lint !e946 MISRA exception justified as comparison of pointers is the cleanest solution. */
                        pxQueue->u.pcReadFrom = ( pxQueue->pcTail - pxQueue->uxItemSize );
                }

                if( xPosition == queueOVERWRITE ) {
                        if( pxQueue->uxMessagesWaiting > ( unsigned portBASE_TYPE ) 0 ) {
                                /* An item is not being added but overwritten, so subtract
                                one from the recorded number of items in the queue so when
                                one is added again below the number of recorded items remains
                                correct. */
                                --( pxQueue->uxMessagesWaiting );
                        }
                }
        }

        ++( pxQueue->uxMessagesWaiting );
}
/*-----------------------------------------------------------*/

static void prvCopyDataFromQueue( xQUEUE * const pxQueue, void * const pvBuffer )
{
        if( pxQueue->uxQueueType != queueQUEUE_IS_MUTEX ) {
                pxQueue->u.pcReadFrom += pxQueue->uxItemSize;
                if( pxQueue->u.pcReadFrom >= pxQueue->pcTail ) { /*lint !e946 MISRA exception justified as use of the relational operator is the cleanest solutions. */
                        pxQueue->u.pcReadFrom = pxQueue->pcHead;
                }
                ( void ) memcpy( ( void * ) pvBuffer, ( void * ) pxQueue->u.pcReadFrom, ( size_t ) pxQueue->uxItemSize ); /*lint !e961 !e418 MISRA exception as the casts are only redundant for some ports.  Also previous logic ensures a null pointer can only be passed to memcpy() when the count is 0. */
        }
}
/*-----------------------------------------------------------*/

static void prvUnlockQueue( xQUEUE *pxQueue )
{
        /* THIS FUNCTION MUST BE CALLED WITH THE SCHEDULER SUSPENDED. */

        /* The lock counts contains the number of extra data items placed or
        removed from the queue while the queue was locked.  When a queue is
        locked items can be added or removed, but the event lists cannot be
        updated. */
        taskENTER_CRITICAL();
        {
                /* See if data was added to the queue while it was locked. */
                while( pxQueue->xTxLock > queueLOCKED_UNMODIFIED ) {
                        /* Data was posted while the queue was locked.  Are any tasks
                        blocked waiting for data to become available? */
#if ( configUSE_QUEUE_SETS == 1 )
                        {
                                if( pxQueue->pxQueueSetContainer != NULL ) {
                                        if( prvNotifyQueueSetContainer( pxQueue, queueSEND_TO_BACK ) == pdTRUE ) {
                                                /* The queue is a member of a queue set, and posting to
                                                the queue set caused a higher priority task to unblock.
                                                A context switch is required. */
                                                vTaskMissedYield();
                                        }
                                } else {
                                        /* Tasks that are removed from the event list will get added to
                                        the pending ready list as the scheduler is still suspended. */
                                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                                        /* The task waiting has a higher priority so record that a
                                                        context	switch is required. */
                                                        vTaskMissedYield();
                                                }
                                        } else {
                                                break;
                                        }
                                }
                        }
#else /* configUSE_QUEUE_SETS */
                        {
                                /* Tasks that are removed from the event list will get added to
                                the pending ready list as the scheduler is still suspended. */
                                if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                                /* The task waiting has a higher priority so record that a
                                                context	switch is required. */
                                                vTaskMissedYield();
                                        }
                                } else {
                                        break;
                                }
                        }
#endif /* configUSE_QUEUE_SETS */

                        --( pxQueue->xTxLock );
                }

                pxQueue->xTxLock = queueUNLOCKED;
        }
        taskEXIT_CRITICAL();

        /* Do the same for the Rx lock. */
        taskENTER_CRITICAL();
        {
                while( pxQueue->xRxLock > queueLOCKED_UNMODIFIED ) {
                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE ) {
                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != pdFALSE ) {
                                        vTaskMissedYield();
                                }

                                --( pxQueue->xRxLock );
                        } else {
                                break;
                        }
                }

                pxQueue->xRxLock = queueUNLOCKED;
        }
        taskEXIT_CRITICAL();
}
/*-----------------------------------------------------------*/

static signed portBASE_TYPE prvIsQueueEmpty( const xQUEUE *pxQueue )
{
        signed portBASE_TYPE xReturn;

        taskENTER_CRITICAL();
        {
                if( pxQueue->uxMessagesWaiting == ( unsigned portBASE_TYPE )  0 ) {
                        xReturn = pdTRUE;
                } else {
                        xReturn = pdFALSE;
                }
        }
        taskEXIT_CRITICAL();

        return xReturn;
}
/*-----------------------------------------------------------*/

signed portBASE_TYPE xQueueIsQueueEmptyFromISR( const xQueueHandle xQueue )
{
        signed portBASE_TYPE xReturn;

        configASSERT( xQueue );
        if( ( ( xQUEUE * ) xQueue )->uxMessagesWaiting == ( unsigned portBASE_TYPE ) 0 ) {
                xReturn = pdTRUE;
        } else {
                xReturn = pdFALSE;
        }

        return xReturn;
} /*lint !e818 xQueue could not be pointer to const because it is a typedef. */
/*-----------------------------------------------------------*/

static signed portBASE_TYPE prvIsQueueFull( const xQUEUE *pxQueue )
{
        signed portBASE_TYPE xReturn;

        taskENTER_CRITICAL();
        {
                if( pxQueue->uxMessagesWaiting == pxQueue->uxLength ) {
                        xReturn = pdTRUE;
                } else {
                        xReturn = pdFALSE;
                }
        }
        taskEXIT_CRITICAL();

        return xReturn;
}
/*-----------------------------------------------------------*/

signed portBASE_TYPE xQueueIsQueueFullFromISR( const xQueueHandle xQueue )
{
        signed portBASE_TYPE xReturn;

        configASSERT( xQueue );
        if( ( ( xQUEUE * ) xQueue )->uxMessagesWaiting == ( ( xQUEUE * ) xQueue )->uxLength ) {
                xReturn = pdTRUE;
        } else {
                xReturn = pdFALSE;
        }

        return xReturn;
} /*lint !e818 xQueue could not be pointer to const because it is a typedef. */
/*-----------------------------------------------------------*/

#if ( configUSE_CO_ROUTINES == 1 )

signed portBASE_TYPE xQueueCRSend( xQueueHandle xQueue, const void *pvItemToQueue, portTickType xTicksToWait )
{
        signed portBASE_TYPE xReturn;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        /* If the queue is already full we may have to block.  A critical section
        is required to prevent an interrupt removing something from the queue
        between the check to see if the queue is full and blocking on the queue. */
        portDISABLE_INTERRUPTS();
        {
                if( prvIsQueueFull( pxQueue ) != pdFALSE ) {
                        /* The queue is full - do we want to block or just leave without
                        posting? */
                        if( xTicksToWait > ( portTickType ) 0 ) {
                                /* As this is called from a coroutine we cannot block directly, but
                                return indicating that we need to block. */
                                vCoRoutineAddToDelayedList( xTicksToWait, &( pxQueue->xTasksWaitingToSend ) );
                                portENABLE_INTERRUPTS();
                                return errQUEUE_BLOCKED;
                        } else {
                                portENABLE_INTERRUPTS();
                                return errQUEUE_FULL;
                        }
                }
        }
        portENABLE_INTERRUPTS();

        portDISABLE_INTERRUPTS();
        {
                if( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) {
                        /* There is room in the queue, copy the data into the queue. */
                        prvCopyDataToQueue( pxQueue, pvItemToQueue, queueSEND_TO_BACK );
                        xReturn = pdPASS;

                        /* Were any co-routines waiting for data to become available? */
                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                /* In this instance the co-routine could be placed directly
                                into the ready list as we are within a critical section.
                                Instead the same pending ready list mechanism is used as if
                                the event were caused from within an interrupt. */
                                if( xCoRoutineRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                        /* The co-routine waiting has a higher priority so record
                                        that a yield might be appropriate. */
                                        xReturn = errQUEUE_YIELD;
                                }
                        }
                } else {
                        xReturn = errQUEUE_FULL;
                }
        }
        portENABLE_INTERRUPTS();

        return xReturn;
}

#endif /* configUSE_CO_ROUTINES */
/*-----------------------------------------------------------*/

#if ( configUSE_CO_ROUTINES == 1 )

signed portBASE_TYPE xQueueCRReceive( xQueueHandle xQueue, void *pvBuffer, portTickType xTicksToWait )
{
        signed portBASE_TYPE xReturn;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        /* If the queue is already empty we may have to block.  A critical section
        is required to prevent an interrupt adding something to the queue
        between the check to see if the queue is empty and blocking on the queue. */
        portDISABLE_INTERRUPTS();
        {
                if( pxQueue->uxMessagesWaiting == ( unsigned portBASE_TYPE ) 0 ) {
                        /* There are no messages in the queue, do we want to block or just
                        leave with nothing? */
                        if( xTicksToWait > ( portTickType ) 0 ) {
                                /* As this is a co-routine we cannot block directly, but return
                                indicating that we need to block. */
                                vCoRoutineAddToDelayedList( xTicksToWait, &( pxQueue->xTasksWaitingToReceive ) );
                                portENABLE_INTERRUPTS();
                                return errQUEUE_BLOCKED;
                        } else {
                                portENABLE_INTERRUPTS();
                                return errQUEUE_FULL;
                        }
                }
        }
        portENABLE_INTERRUPTS();

        portDISABLE_INTERRUPTS();
        {
                if( pxQueue->uxMessagesWaiting > ( unsigned portBASE_TYPE ) 0 ) {
                        /* Data is available from the queue. */
                        pxQueue->u.pcReadFrom += pxQueue->uxItemSize;
                        if( pxQueue->u.pcReadFrom >= pxQueue->pcTail ) {
                                pxQueue->u.pcReadFrom = pxQueue->pcHead;
                        }
                        --( pxQueue->uxMessagesWaiting );
                        ( void ) memcpy( ( void * ) pvBuffer, ( void * ) pxQueue->u.pcReadFrom, ( unsigned ) pxQueue->uxItemSize );

                        xReturn = pdPASS;

                        /* Were any co-routines waiting for space to become available? */
                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE ) {
                                /* In this instance the co-routine could be placed directly
                                into the ready list as we are within a critical section.
                                Instead the same pending ready list mechanism is used as if
                                the event were caused from within an interrupt. */
                                if( xCoRoutineRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != pdFALSE ) {
                                        xReturn = errQUEUE_YIELD;
                                }
                        }
                } else {
                        xReturn = pdFAIL;
                }
        }
        portENABLE_INTERRUPTS();

        return xReturn;
}

#endif /* configUSE_CO_ROUTINES */
/*-----------------------------------------------------------*/

#if ( configUSE_CO_ROUTINES == 1 )

signed portBASE_TYPE xQueueCRSendFromISR( xQueueHandle xQueue, const void *pvItemToQueue, signed portBASE_TYPE xCoRoutinePreviouslyWoken )
{
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        /* Cannot block within an ISR so if there is no space on the queue then
        exit without doing anything. */
        if( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) {
                prvCopyDataToQueue( pxQueue, pvItemToQueue, queueSEND_TO_BACK );

                /* We only want to wake one co-routine per ISR, so check that a
                co-routine has not already been woken. */
                if( xCoRoutinePreviouslyWoken == pdFALSE ) {
                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE ) {
                                if( xCoRoutineRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                        return pdTRUE;
                                }
                        }
                }
        }

        return xCoRoutinePreviouslyWoken;
}

#endif /* configUSE_CO_ROUTINES */
/*-----------------------------------------------------------*/

#if ( configUSE_CO_ROUTINES == 1 )

signed portBASE_TYPE xQueueCRReceiveFromISR( xQueueHandle xQueue, void *pvBuffer, signed portBASE_TYPE *pxCoRoutineWoken )
{
        signed portBASE_TYPE xReturn;
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        /* We cannot block from an ISR, so check there is data available. If
        not then just leave without doing anything. */
        if( pxQueue->uxMessagesWaiting > ( unsigned portBASE_TYPE ) 0 ) {
                /* Copy the data from the queue. */
                pxQueue->u.pcReadFrom += pxQueue->uxItemSize;
                if( pxQueue->u.pcReadFrom >= pxQueue->pcTail ) {
                        pxQueue->u.pcReadFrom = pxQueue->pcHead;
                }
                --( pxQueue->uxMessagesWaiting );
                ( void ) memcpy( ( void * ) pvBuffer, ( void * ) pxQueue->u.pcReadFrom, ( unsigned ) pxQueue->uxItemSize );

                if( ( *pxCoRoutineWoken ) == pdFALSE ) {
                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE ) {
                                if( xCoRoutineRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != pdFALSE ) {
                                        *pxCoRoutineWoken = pdTRUE;
                                }
                        }
                }

                xReturn = pdPASS;
        } else {
                xReturn = pdFAIL;
        }

        return xReturn;
}

#endif /* configUSE_CO_ROUTINES */
/*-----------------------------------------------------------*/

#if ( configQUEUE_REGISTRY_SIZE > 0 )

void vQueueAddToRegistry( xQueueHandle xQueue, signed char *pcQueueName )
{
        unsigned portBASE_TYPE ux;

        /* See if there is an empty space in the registry.  A NULL name denotes
        a free slot. */
        for( ux = ( unsigned portBASE_TYPE ) 0U; ux < ( unsigned portBASE_TYPE ) configQUEUE_REGISTRY_SIZE; ux++ ) {
                if( xQueueRegistry[ ux ].pcQueueName == NULL ) {
                        /* Store the information on this queue. */
                        xQueueRegistry[ ux ].pcQueueName = pcQueueName;
                        xQueueRegistry[ ux ].xHandle = xQueue;
                        break;
                }
        }
}

#endif /* configQUEUE_REGISTRY_SIZE */
/*-----------------------------------------------------------*/

#if ( configQUEUE_REGISTRY_SIZE > 0 )

void vQueueUnregisterQueue( xQueueHandle xQueue )
{
        unsigned portBASE_TYPE ux;

        /* See if the handle of the queue being unregistered in actually in the
        registry. */
        for( ux = ( unsigned portBASE_TYPE ) 0U; ux < ( unsigned portBASE_TYPE ) configQUEUE_REGISTRY_SIZE; ux++ ) {
                if( xQueueRegistry[ ux ].xHandle == xQueue ) {
                        /* Set the name to NULL to show that this slot if free again. */
                        xQueueRegistry[ ux ].pcQueueName = NULL;
                        break;
                }
        }

} /*lint !e818 xQueue could not be pointer to const because it is a typedef. */

#endif /* configQUEUE_REGISTRY_SIZE */
/*-----------------------------------------------------------*/

#if ( configUSE_TIMERS == 1 )

void vQueueWaitForMessageRestricted( xQueueHandle xQueue, portTickType xTicksToWait )
{
        xQUEUE * const pxQueue = ( xQUEUE * ) xQueue;

        /* This function should not be called by application code hence the
        'Restricted' in its name.  It is not part of the public API.  It is
        designed for use by kernel code, and has special calling requirements.
        It can result in vListInsert() being called on a list that can only
        possibly ever have one item in it, so the list will be fast, but even
        so it should be called with the scheduler locked and not from a critical
        section. */

        /* Only do anything if there are no messages in the queue.  This function
        will not actually cause the task to block, just place it on a blocked
        list.  It will not block until the scheduler is unlocked - at which
        time a yield will be performed.  If an item is added to the queue while
        the queue is locked, and the calling task blocks on the queue, then the
        calling task will be immediately unblocked when the queue is unlocked. */
        prvLockQueue( pxQueue );
        if( pxQueue->uxMessagesWaiting == ( unsigned portBASE_TYPE ) 0U ) {
                /* There is nothing in the queue, block for the specified period. */
                vTaskPlaceOnEventListRestricted( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait );
        }
        prvUnlockQueue( pxQueue );
}

#endif /* configUSE_TIMERS */
/*-----------------------------------------------------------*/

#if ( configUSE_QUEUE_SETS == 1 )

xQueueSetHandle xQueueCreateSet( unsigned portBASE_TYPE uxEventQueueLength )
{
        xQueueSetHandle pxQueue;

        pxQueue = xQueueGenericCreate( uxEventQueueLength, sizeof( xQUEUE * ), queueQUEUE_TYPE_SET );

        return pxQueue;
}

#endif /* configUSE_QUEUE_SETS */
/*-----------------------------------------------------------*/

#if ( configUSE_QUEUE_SETS == 1 )

portBASE_TYPE xQueueAddToSet( xQueueSetMemberHandle xQueueOrSemaphore, xQueueSetHandle xQueueSet )
{
        portBASE_TYPE xReturn;

        if( ( ( xQUEUE * ) xQueueOrSemaphore )->pxQueueSetContainer != NULL ) {
                /* Cannot add a queue/semaphore to more than one queue set. */
                xReturn = pdFAIL;
        } else if( ( ( xQUEUE * ) xQueueOrSemaphore )->uxMessagesWaiting != ( unsigned portBASE_TYPE ) 0 ) {
                /* Cannot add a queue/semaphore to a queue set if there are already
                items in the queue/semaphore. */
                xReturn = pdFAIL;
        } else {
                taskENTER_CRITICAL();
                {
                        ( ( xQUEUE * ) xQueueOrSemaphore )->pxQueueSetContainer = xQueueSet;
                }
                taskEXIT_CRITICAL();
                xReturn = pdPASS;
        }

        return xReturn;
}

#endif /* configUSE_QUEUE_SETS */
/*-----------------------------------------------------------*/

#if ( configUSE_QUEUE_SETS == 1 )

portBASE_TYPE xQueueRemoveFromSet( xQueueSetMemberHandle xQueueOrSemaphore, xQueueSetHandle xQueueSet )
{
        portBASE_TYPE xReturn;
        xQUEUE * const pxQueueOrSemaphore = ( xQUEUE * ) xQueueOrSemaphore;

        if( pxQueueOrSemaphore->pxQueueSetContainer != xQueueSet ) {
                /* The queue was not a member of the set. */
                xReturn = pdFAIL;
        } else if( pxQueueOrSemaphore->uxMessagesWaiting != ( unsigned portBASE_TYPE ) 0 ) {
                /* It is dangerous to remove a queue from a set when the queue is
                not empty because the queue set will still hold pending events for
                the queue. */
                xReturn = pdFAIL;
        } else {
                taskENTER_CRITICAL();
                {
                        /* The queue is no longer contained in the set. */
                        pxQueueOrSemaphore->pxQueueSetContainer = NULL;
                }
                taskEXIT_CRITICAL();
                xReturn = pdPASS;
        }

        return xReturn;
} /*lint !e818 xQueueSet could not be declared as pointing to const as it is a typedef. */

#endif /* configUSE_QUEUE_SETS */
/*-----------------------------------------------------------*/

#if ( configUSE_QUEUE_SETS == 1 )

xQueueSetMemberHandle xQueueSelectFromSet( xQueueSetHandle xQueueSet, portTickType xBlockTimeTicks )
{
        xQueueSetMemberHandle xReturn = NULL;

        ( void ) xQueueGenericReceive( ( xQueueHandle ) xQueueSet, &xReturn, xBlockTimeTicks, pdFALSE ); /*lint !e961 Casting from one typedef to another is not redundant. */
        return xReturn;
}

#endif /* configUSE_QUEUE_SETS */
/*-----------------------------------------------------------*/

#if ( configUSE_QUEUE_SETS == 1 )

xQueueSetMemberHandle xQueueSelectFromSetFromISR( xQueueSetHandle xQueueSet )
{
        xQueueSetMemberHandle xReturn = NULL;

        ( void ) xQueueReceiveFromISR( ( xQueueHandle ) xQueueSet, &xReturn, NULL ); /*lint !e961 Casting from one typedef to another is not redundant. */
        return xReturn;
}

#endif /* configUSE_QUEUE_SETS */
/*-----------------------------------------------------------*/

#if ( configUSE_QUEUE_SETS == 1 )

static portBASE_TYPE prvNotifyQueueSetContainer( const xQUEUE * const pxQueue, portBASE_TYPE xCopyPosition )
{
        xQUEUE *pxQueueSetContainer = pxQueue->pxQueueSetContainer;
        portBASE_TYPE xReturn = pdFALSE;

        configASSERT( pxQueueSetContainer );
        configASSERT( pxQueueSetContainer->uxMessagesWaiting < pxQueueSetContainer->uxLength );

        if( pxQueueSetContainer->uxMessagesWaiting < pxQueueSetContainer->uxLength ) {
                traceQUEUE_SEND( pxQueueSetContainer );
                /* The data copies is the handle of the queue that contains data. */
                prvCopyDataToQueue( pxQueueSetContainer, &pxQueue, xCopyPosition );
                if( listLIST_IS_EMPTY( &( pxQueueSetContainer->xTasksWaitingToReceive ) ) == pdFALSE ) {
                        if( xTaskRemoveFromEventList( &( pxQueueSetContainer->xTasksWaitingToReceive ) ) != pdFALSE ) {
                                /* The task waiting has a higher priority */
                                xReturn = pdTRUE;
                        }
                }
        }

        return xReturn;
}

#endif /* configUSE_QUEUE_SETS */












