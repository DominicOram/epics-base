
/* * $Id$
 *
 *                    L O S  A L A M O S
 *              Los Alamos National Laboratory
 *               Los Alamos, New Mexico 87545
 *
 *  Copyright, 1986, The Regents of the University of California.
 *
 *  Author: Jeff Hill
 */

#include "iocinf.h"

//
// searchTimer::searchTimer ()
//
searchTimer::searchTimer (udpiiu &iiuIn, osiTimerQueue &queueIn) :
    osiTimer (queueIn),
    iiu (iiuIn),
    framesPerTry (INITIALTRIESPERFRAME),
    framesPerTryCongestThresh (UINT_MAX),
    minRetry (UINT_MAX),
    retry (0u),
    searchTries (0u),
    searchResponses (0u),
    retrySeqNo (0u),
    retrySeqNoAtListBegin (0u),
    period (CA_RECAST_DELAY)
{
}

//
// searchTimer::reset ()
//
void searchTimer::reset (double period)
{
    LOCK (this->iiu.niiu.iiu.pcas);
    this->retry = 0;
    this->period = period;
    UNLOCK (this->iiu.niiu.iiu.pcas);

    if (this->timeRemaining()>period) {
        this->reschedule (0.0);
    }
}

/* 
 * searchTimer::setRetryInterval ()
 */
void searchTimer::setRetryInterval (unsigned retryNo)
{
    unsigned idelay;
    double delay;

    LOCK (this->iiu.niiu.iiu.pcas);

    /*
     * set the retry number
     */
    this->retry = min (retryNo, MAXCONNTRIES+1u);

    /*
     * set the retry interval
     */
    idelay = 1u << min (this->retry, CHAR_BIT*sizeof(idelay)-1u);
    delay = idelay * CA_RECAST_DELAY; /* sec */ 
    /*
     * place upper limit on the retry delay
     */
    this->period = min (CA_RECAST_PERIOD, delay);

    UNLOCK (this->iiu.niiu.iiu.pcas);

#ifdef DEBUG
    printf ("new CA search period is %f sec\n", this->period);
#endif  
}

//
// searchTimer::notifySearchResponse ()
//
// Reset the delay to the next search request if we get
// at least one response. However, dont reset this delay if we
// get a delayed response to an old search request.
//
void searchTimer::notifySearchResponse (nciu *pChan)
{
    LOCK (this->iiu.niiu.iiu.pcas);

    if ( this->retrySeqNoAtListBegin <= pChan->retrySeqNo ) {
        if ( this->searchResponses < ULONG_MAX ) {
            this->searchResponses++;
        }
    }    
        
    UNLOCK (this->iiu.niiu.iiu.pcas);

    if (pChan->retrySeqNo == this->retrySeqNo) {
        this->reschedule (0.0);
    }
}


//
// searchTimer::expire ()
//
void searchTimer::expire ()
{
    nciu        *chan;
    nciu        *firstChan;
    int         status;
    unsigned    nSent=0u;
    
    /*
     * check to see if there is nothing to do here 
     */
    if (ellCount(&this->iiu.niiu.chidList)==0) {
        return;
    }   
    
    LOCK (this->iiu.niiu.iiu.pcas);
 
    /*
     * increment the retry sequence number
     */
    this->retrySeqNo++; /* allowed to roll over */
    
    /*
     * dynamically adjust the number of UDP frames per 
     * try depending how many search requests are not 
     * replied to
     *
     * This determines how many search request can be 
     * sent together (at the same instant in time).
     *
     * The variable this->framesPerTry
     * determines the number of UDP frames to be sent
     * each time that retrySearchRequest() is called.
     * If this value is too high we will waste some
     * network bandwidth. If it is too low we will
     * use very little of the incoming UDP message
     * buffer associated with the server's port and
     * will therefore take longer to connect. We 
     * initialize this->framesPerTry
     * to a prime number so that it is less likely that the
     * same channel is in the last UDP frame
     * sent every time that this is called (and
     * potentially discarded by a CA server with
     * a small UDP input queue). 
     */
    /*
     * increase frames per try only if we see better than
     * a 93.75% success rate for one pass through the list
     */
    if (this->searchResponses >
        (this->searchTries-(this->searchTries/16u)) ) {
        /*
         * increase UDP frames per try if we have a good score
         */
        if ( this->framesPerTry < MAXTRIESPERFRAME ) {
            /*
             * a congestion avoidance threshold similar to TCP is now used
             */
            if ( this->framesPerTry < this->framesPerTryCongestThresh ) {
                this->framesPerTry += this->framesPerTry;
            }
            else {
                this->framesPerTry += (this->framesPerTry/8) + 1;
            }
#if 0
            printf ("Increasing frame count to %u t=%u r=%u\n", 
                this->framesPerTry, this->searchTries, 
                this->searchResponses);
#endif
        }
    }
    /*
     * if we detect congestion because we have less than a 87.5% success 
     * rate then gradually reduce the frames per try
     */
    else if ( this->searchResponses < 
        (this->searchTries-(this->searchTries/8u)) ) {
            if (this->framesPerTry>1) {
                this->framesPerTry--;
            }
            this->framesPerTryCongestThresh = this->framesPerTry/2 + 1;
#if 0
            printf ("Congestion detected - set frames per try to %u t=%u r=%u\n", 
                this->framesPerTry, this->searchTries, 
                this->searchResponses);
#endif
    }
    
    /*
     * a successful cac_search_msg() sends channel to
     * the end of the list
     */
    firstChan = chan = (nciu *) ellFirst (&this->iiu.niiu.chidList);
    while (chan) {
        
        this->minRetry = min (this->minRetry, chan->retry);
        
        /*
         * clear counter when we reach the end of the list
         *
         * if we are making some progress then
         * dont increase the delay between search
         * requests
         */
        if ( this->iiu.niiu.iiu.pcas->ca_pEndOfBCastList == chan ) {
            if ( this->searchResponses == 0u ) {
#if 0
                printf ("increasing search try interval\n");
#endif
                this->setRetryInterval (this->minRetry + 1u);
            }
            
            this->minRetry = UINT_MAX;
            
            /*
             * increment the retry sequence number
             * (this prevents the time of the next search
             * try from being set to the current time if
             * we are handling a response from an old
             * search message)
             */
            this->retrySeqNo++; /* allowed to roll over */
            
            /*
             * so that old search tries will not update the counters
             */
            this->retrySeqNoAtListBegin = this->retrySeqNo;

            /*
             * reset the search try/response counters at the end of the list
             * (sequence number) so that we dont overflow, but dont subtract
             * out tries that dont have a matching response yet in case they
             * are delayed
             */
            if ( this->searchTries > this->searchResponses ) {
                this->searchTries -= this->searchResponses;
            }
            else {
                this->searchTries = 0;
            }
            this->searchResponses = 0;

#if 0
            printf ("saw end of list\n");
#endif  
        }
        
        /*
         * this moves the channel to the end of the
         * list (if successful)
         */
        status = cac_search_msg (chan);
        if (status != ECA_NORMAL) {
            nSent++;
            
            if (nSent>=this->framesPerTry) {
                break;
            }

            /* flush out the search request buffer */
            semBinaryGive (this->iiu.xmitSignal);
            
            /* try again */
            status = cac_search_msg (chan);
            if (status != ECA_NORMAL) {
                break;
            }
        }

        if (this->searchTries<ULONG_MAX) {
            this->searchTries++;
        }

        chan->retrySeqNo = this->retrySeqNo;
        chan = (nciu *) ellFirst (&this->iiu.niiu.chidList);
        
        /*
         * dont send any of the channels twice within one try
         */
        if (chan==firstChan) {
            /*
             * add one to nSent because there may be 
             * one more partial frame to be sent
             */
            nSent++;
            
            /* 
             * cap this->framesPerTry to
             * the number of frames required for all of 
             * the unresolved channels
             */
            if (this->framesPerTry>nSent) {
                this->framesPerTry = nSent;
            }
            
            break;
        }
    }
    
    UNLOCK (this->iiu.niiu.iiu.pcas);

    /* flush out the search request buffer */
    semBinaryGive (this->iiu.xmitSignal);
    
#ifdef DEBUG
    printf ("sent %u delay sec=%f\n", nSent, this->period);
#endif

}

void searchTimer::destroy ()
{
}

bool searchTimer::again () const
{
    if (ellCount(&this->iiu.niiu.chidList)==0) {
        return false;
    }
    else {
        if (this->retry < MAXCONNTRIES) {
            return true;
        }
        else {
            return false;
        }
    }
}

double searchTimer::delay () const
{
    return this->period;
}

void searchTimer::show (unsigned level) const
{
}

const char *searchTimer::name () const
{
    return "CAC Search Timer";
}
