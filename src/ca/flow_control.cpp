/* $Id$ */
/*
 *
 *                    L O S  A L A M O S
 *              Los Alamos National Laboratory
 *               Los Alamos, New Mexico 87545
 *
 *  Copyright, 1986, The Regents of the University of California.
 *
 *  Author: Jeff Hill
 */	

#include		"iocinf.h"

/*
 * FLOW CONTROL
 * 
 * Keep track of how many times messages have 
 * come with out a break in between and 
 * suppress monitors if we are behind
 * (an update is sent when we catch up)
 */

void flow_control_on (tcpiiu *piiu)
{
	int status;

	LOCK (piiu->niiu.iiu.pcas);

	/*	
	 * I prefer to avoid going into flow control 
	 * as this impacts the performance of batched fetches
	 */
	if (piiu->contiguous_msg_count >= MAX_CONTIGUOUS_MSG_COUNT) {
		if (!piiu->client_busy) {
			status = ca_busy_message(piiu);
			if (status==ECA_NORMAL) {
				assert(piiu->niiu.iiu.pcas->ca_number_iiu_in_fc<UINT_MAX);
				piiu->niiu.iiu.pcas->ca_number_iiu_in_fc++;
				piiu->client_busy = TRUE;
#				if defined(DEBUG) 
					printf("fc on\n");
#				endif
			}
		}
	}
	else {
		piiu->contiguous_msg_count++;
	}

	UNLOCK (piiu->niiu.iiu.pcas);
	return;
}

void flow_control_off (tcpiiu *piiu)
{
	int    		status;

	LOCK (piiu->niiu.iiu.pcas);

	piiu->contiguous_msg_count = 0;
	if (piiu->client_busy) {
		status = ca_ready_message(piiu);
		if (status==ECA_NORMAL) {
			assert (piiu->niiu.iiu.pcas->ca_number_iiu_in_fc>0u);
			piiu->niiu.iiu.pcas->ca_number_iiu_in_fc--;
			piiu->client_busy = FALSE;
#			if defined(DEBUG) 
				printf("fc off\n");
#			endif
		}
	}

	UNLOCK (piiu->niiu.iiu.pcas);
	return;
}
