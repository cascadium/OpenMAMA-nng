/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Frank Quinn (http://fquinner.github.io)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*=========================================================================
  =                             Includes                                  =
  =========================================================================*/

#include <stdint.h>
#include <mama/mama.h>
#include <mama/integration/mama.h>
#include <mama/integration/queue.h>
#include <mama/integration/msg.h>
#include <mama/integration/subscription.h>
#include <mama/integration/transport.h>
#include <timers.h>
#include <stdio.h>
#include <errno.h>
#include <wombat/queue.h>
#include "transport.h"
#include "nng.h"
#include "msg.h"
#include <mama/integration/endpointpool.h>
#include <mama/integration/bridge/base.h>
#include "nngbridgefunctions.h"
#include <nng/nng.h>
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/pubsub0/sub.h>
#include <errno.h>
#include <wombat/mempool.h>
#include <wombat/memnode.h>


/*=========================================================================
  =                              Macros                                   =
  =========================================================================*/

/* Transport configuration parameters */
#define     TPORT_PARAM_PREFIX                  "mama.nng.transport"
#define     TPORT_PARAM_OUTGOING_URL            "outgoing_url"
#define     TPORT_PARAM_INCOMING_URL            "incoming_url"
#define     TPORT_PARAM_MSG_POOL_SIZE           "msg_pool_size"
#define     TPORT_PARAM_MSG_NODE_SIZE           "msg_node_size"

/* Default values for corresponding configuration parameters */
#define     DEFAULT_SUB_OUTGOING_URL        "tcp://*:5557"
#define     DEFAULT_SUB_INCOMING_URL        "tcp://127.0.0.1:5556"
#define     DEFAULT_PUB_OUTGOING_URL        "tcp://*:5556"
#define     DEFAULT_PUB_INCOMING_URL        "tcp://127.0.0.1:5557"
#define     DEFAULT_MEMPOOL_SIZE            "1024"
#define     DEFAULT_MEMNODE_SIZE            "4096"

/* Non configurable runtime defaults */
#define     PARAM_NAME_MAX_LENGTH           1024L

/*=========================================================================
  =                  Private implementation prototypes                    =
  =========================================================================*/

/**
 * This function is called in the create function and is responsible for
 * actually subscribing to any transport level data sources and forking off the
 * recv dispatch thread for proton.
 *
 * @param impl  Qpid transport bridge to start
 *
 * @return mama_status indicating whether the method succeeded or failed.
 */
static mama_status
nngBridgeMamaTransportImpl_start (nngTransportBridge* impl);

/**
 * This function is called in the destroy function and is responsible for
 * stopping the proton messengers and joining back with the recv thread created
 * in nngBridgeMamaTransportImpl_start.
 *
 * @param impl  Qpid transport bridge to start
 *
 * @return mama_status indicating whether the method succeeded or failed.
 */
static mama_status
nngBridgeMamaTransportImpl_stop (nngTransportBridge* impl);

/**
 * This function is a queue callback which is enqueued in the recv thread and
 * is then fired once it has reached the head of the queue.
 *
 * @param queue   MAMA queue from which this callback was fired
 * @param closure In this instance, the closure is the nngMsgNode which was
 *                pulled from the pool in the recv callback and then sent
 *                down the MAMA queue.
 *
 * @return mama_status indicating whether the method succeeded or failed.
 */
static void MAMACALLTYPE
nngBridgeMamaTransportImpl_queueCallback (mamaQueue queue, void* closure);

/**
 * This is a local function for parsing long configuration parameters from the
 * MAMA properties object, and supports minimum and maximum limits as well
 * as default values to reduce the amount of code duplicated throughout.
 *
 * @param defaultVal This is the default value to use if the parameter does not
 *                   exist in the configuration file
 * @param minimum    If the parameter obtained from the configuration file is
 *                   less than this value, this value will be used.
 * @param maximum    If the parameter obtained from the configuration file is
 *                   greater than this value, this value will be used.
 * @param format     This is the format string which is used to build the
 *                   name of the configuration parameter which is to be parsed.
 * @param ...        This is the variable list of arguments to be used along
 *                   with the format string.
 *
 * @return long int containing the paramter value, default, minimum or maximum.
 */
/*
static long int
nngBridgeMamaTransportImpl_getParameterAsLong (long        defaultVal,
                                               long        minimum,
                                               long        maximum,
                                               const char* format,
                                               ...);
*/
/**
 * This is a local function for parsing string configuration parameters from the
 * MAMA properties object, and supports default values. This function should
 * be used where the configuration parameter itself can be variable.
 *
 * @param defaultVal This is the default value to use if the parameter does not
 *                   exist in the configuration file
 * @param paramName  The format and variable list combine to form the real
 *                   configuration parameter used. This configuration parameter
 *                   will be stored at this location so the calling function
 *                   can log this.
 * @param format     This is the format string which is used to build the
 *                   name of the configuration parameter which is to be parsed.
 * @param ...        This is the variable list of arguments to be used along
 *                   with the format string.
 *
 * @return const char* containing the parameter value or the default.
 */
static const char*
nngBridgeMamaTransportImpl_getParameterWithVaList (char*       defaultVal,
                                                   char*       paramName,
                                                   const char* format,
                                                   va_list     arguments);

/**
 * This is a local function for parsing string configuration parameters from the
 * MAMA properties object, and supports default values. This function should
 * be used where the configuration parameter itself can be variable.
 *
 * @param defaultVal This is the default value to use if the parameter does not
 *                   exist in the configuration file
 * @param format     This is the format string which is used to build the
 *                   name of the configuration parameter which is to be parsed.
 * @param ...        This is the variable list of arguments to be used along
 *                   with the format string.
 *
 * @return const char* containing the parameter value or the default.
 */
static const char*
nngBridgeMamaTransportImpl_getParameter (const char* defaultVal,
                                         const char* format,
                                         ...);

/**
 * This function is called on its own thread to run the main recv dispatch
 * for all messages coming off the mIncoming messenger. This function is
 * responsible for routing all incoming messages to their required destination
 * and parsing all administrative messages.
 *
 * @param closure    In this case, the closure refers to the nngTransportBridge
 */
static void*
nngBridgeMamaTransportImpl_dispatchThread (void* closure);

void MAMACALLTYPE
nngBridgeMamaTransportImpl_queueClosureCleanupCb (void* closure);

/*=========================================================================
  =               Public interface implementation functions               =
  =========================================================================*/

int
nngBridgeMamaTransport_isValid (transportBridge transport)
{
    nngTransportBridge*    impl   = (nngTransportBridge*) transport;
    int                    status = 0;

    if (NULL != impl)
    {
        status = impl->mIsValid;
    }
    return status;
}

mama_status
nngBridgeMamaTransport_destroy (transportBridge transport)
{
    nngTransportBridge*    impl    = NULL;
    mama_status            status  = MAMA_STATUS_OK;

    if (NULL == transport)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    impl  = (nngTransportBridge*) transport;

    status = nngBridgeMamaTransportImpl_stop (impl);

    endpointPool_destroy (impl->mSubEndpoints);
    endpointPool_destroy (impl->mPubEndpoints);

    free (impl);

    return status;
}

mama_status
nngBridgeMamaTransport_create (transportBridge*    result,
                               const char*         name,
                               mamaTransport       parent)
{
    nngTransportBridge*   impl            = NULL;
    mama_status           status          = MAMA_STATUS_OK;
    char*                 mDefIncoming    = NULL;
    char*                 mDefOutgoing    = NULL;
    const char*           uri             = NULL;
    int                   uri_index       = 0;

    if (NULL == result || NULL == name || NULL == parent)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    impl = (nngTransportBridge*) calloc (1, sizeof (nngTransportBridge));

    /* Back reference the MAMA transport */
    impl->mTransport           = parent;

    /* Initialize the dispatch thread pointer */
    impl->mOmnngDispatchThread  = 0;
    impl->mOmnngDispatchStatus  = MAMA_STATUS_OK;
    impl->mName                 = name;

    mama_log (MAMA_LOG_LEVEL_FINE,
              "nngBridgeMamaTransport_create(): Initializing Transportttt %s",
              name);

    impl->mMemoryPoolSize = atol(nngBridgeMamaTransportImpl_getParameter (
            DEFAULT_MEMPOOL_SIZE,
            "%s.%s.%s",
            TPORT_PARAM_PREFIX,
            name,
            TPORT_PARAM_MSG_POOL_SIZE));
	mama_log(MAMA_LOG_LEVEL_FINE,
		"nngBridgeMamaTransport_create():creating endpoint A %s",
		name);
    impl->mMemoryNodeSize = atol(nngBridgeMamaTransportImpl_getParameter (
            DEFAULT_MEMNODE_SIZE,
            "%s.%s.%s",
            TPORT_PARAM_PREFIX,
            name,
            TPORT_PARAM_MSG_NODE_SIZE));

    mama_log (MAMA_LOG_LEVEL_FINE,
              "nngBridgeMamaTransport_create(): Any message pools created will "
              "contain %lu nodes of %lu bytes.",
              name,
              impl->mMemoryPoolSize,
              impl->mMemoryNodeSize);

    if (0 == strcmp(impl->mName, "pub"))
    {
        mDefIncoming = DEFAULT_PUB_INCOMING_URL;
        mDefOutgoing = DEFAULT_PUB_OUTGOING_URL;
    }
    else
    {
        mDefIncoming = DEFAULT_SUB_INCOMING_URL;
        mDefOutgoing = DEFAULT_SUB_OUTGOING_URL;
    }

    /* Start with bare incoming address */
    impl->mIncomingAddress[0] = nngBridgeMamaTransportImpl_getParameter (
                mDefIncoming,
                "%s.%s.%s",
                TPORT_PARAM_PREFIX,
                name,
                TPORT_PARAM_INCOMING_URL);

    /* Now parse any _0, _1 etc. */
    uri_index = 0;
    while (NULL != (uri = nngBridgeMamaTransportImpl_getParameter (
            NULL,
            "%s.%s.%s_%d",
            TPORT_PARAM_PREFIX,
            name,
            TPORT_PARAM_INCOMING_URL,
            uri_index)))
    {
        impl->mIncomingAddress[uri_index] = uri;
        uri_index++;
    }

    /* Start with bare outgoing address */
    impl->mOutgoingAddress[0] = nngBridgeMamaTransportImpl_getParameter (
                mDefOutgoing,
                "%s.%s.%s",
                TPORT_PARAM_PREFIX,
                name,
                TPORT_PARAM_OUTGOING_URL);

    /* Now parse any _0, _1 etc. */
    uri_index = 0;
    while (NULL != (uri = nngBridgeMamaTransportImpl_getParameter (
            NULL,
            "%s.%s.%s_%d",
            TPORT_PARAM_PREFIX,
            name,
            TPORT_PARAM_OUTGOING_URL,
            uri_index)))
    {
        impl->mOutgoingAddress[uri_index] = uri;
        uri_index++;
    }
	mama_log(MAMA_LOG_LEVEL_FINE,
		"nngBridgeMamaTransport_create():creating endpoint 1 %s",
		name);
    status = endpointPool_create (&impl->mSubEndpoints, "mSubEndpoints");
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaTransport_create(): "
                  "Failed to create subscribing endpoints");
        free (impl);
        return MAMA_STATUS_PLATFORM;
    }
	mama_log(MAMA_LOG_LEVEL_FINE,
		"nngBridgeMamaTransport_create(): Creating endpoint 2 %s",
		name);
    status = endpointPool_create (&impl->mPubEndpoints, "mPubEndpoints");
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaTransport_create(): "
                  "Failed to create publishing endpoints");
        free (impl);
        return MAMA_STATUS_PLATFORM;
    }

    impl->mIsValid = 1;

    *result = (transportBridge) impl;

    return nngBridgeMamaTransportImpl_start (impl);
}


/*=========================================================================
  =                  Public implementation functions                      =
  =========================================================================*/

nngTransportBridge*
nngBridgeMamaTransportImpl_getTransportBridge (mamaTransport transport)
{
    nngTransportBridge*    impl;
    mama_status             status = MAMA_STATUS_OK;

    status = mamaTransport_getBridgeTransport (transport,
                                               (transportBridge*) &impl);

    if (status != MAMA_STATUS_OK || impl == NULL)
    {
        return NULL;
    }

    return impl;
}


/*=========================================================================
  =                  Private implementation functions                     =
  =========================================================================*/
mama_status
nngBridgeMamaTransportImpl_start (nngTransportBridge* impl)
{
    int rc = 0;
    int i  = 0;

    if (NULL == impl)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                "nngBridgeMamaTransportImpl_start(): transport NULL");
        return MAMA_STATUS_NULL_ARG;
    }

    nng_pub0_open(&impl->mNngSocketPublisher);
    nng_sub0_open(&impl->mNngSocketSubscriber);

    if (strcmp(impl->mName, "sub") == 0) {
        nng_setopt_ms(impl->mNngSocketSubscriber, NNG_OPT_RECVTIMEO, 100);
        nng_setopt_ms(impl->mNngSocketSubscriber, NNG_OPT_SENDTIMEO, 100);
        nng_dialer_create(&impl->mNngDialerSubscriber, impl->mNngSocketSubscriber, "tcp://127.0.0.1:4545");
        nng_listener_create(&impl->mNngListenerPublisher, impl->mNngSocketPublisher, "tcp://127.0.0.1:4546");
    } else {
        nng_setopt_ms(impl->mNngSocketSubscriber, NNG_OPT_RECVTIMEO, 100);
        nng_setopt_ms(impl->mNngSocketSubscriber, NNG_OPT_SENDTIMEO, 100);
        nng_dialer_create(&impl->mNngDialerSubscriber, impl->mNngSocketSubscriber, "tcp://127.0.0.1:4546");
        nng_listener_create(&impl->mNngListenerPublisher, impl->mNngSocketPublisher, "tcp://127.0.0.1:4545");
    }

    /* Set the transport bridge mIsDispatching to true. */
    impl->mIsDispatching = 1;

    /* Initialize dispatch thread */
    rc = wthread_create (&(impl->mOmnngDispatchThread),
                         NULL,
                         nngBridgeMamaTransportImpl_dispatchThread,
                         impl);
    if (0 != rc)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR, "nngBridgeMamaTransportImpl_start(): "
                  "wthread_create returned %d", rc);
        return MAMA_STATUS_PLATFORM;
    }

    nng_dialer_start(impl->mNngDialerSubscriber, NNG_FLAG_NONBLOCK);
    nng_listener_start(impl->mNngListenerPublisher, NNG_FLAG_NONBLOCK);

    return MAMA_STATUS_OK;
}

mama_status nngBridgeMamaTransportImpl_stop (nngTransportBridge* impl)
{
    /* There are two mechanisms by which we can stop the transport
     * - Send a special message, which will be picked up by recv
     *   For the instance when there is very little data flowing.
     * - Set the mIsDispatching variable in the transportBridge object to
     *   false, for instances when there is a lot of data flowing.
     */
    mama_status     status = MAMA_STATUS_OK;

    /* Set the transportBridge mIsDispatching to false */
    impl->mIsDispatching = 0;

    mama_log (MAMA_LOG_LEVEL_FINEST, "nngBridgeMamaTransportImpl_stop(): "
                  "Waiting on dispatch thread to terminate.");

    wthread_join (impl->mOmnngDispatchThread, NULL);
    status = impl->mOmnngDispatchStatus;

    nng_listener_close(impl->mNngListenerPublisher);
    nng_dialer_close(impl->mNngDialerSubscriber);
    nng_close(impl->mNngSocketPublisher);
    nng_close(impl->mNngSocketSubscriber);

    mama_log (MAMA_LOG_LEVEL_FINEST, "nngBridgeMamaTransportImpl_stop(): "
                      "Rejoined with status: %s.",
                      mamaStatus_stringForStatus(status));

    return MAMA_STATUS_OK;
}

/**
 * Called when message removed from queue by dispatch thread
 *
 * @param data The AMQP payload
 * @param closure The subscriber
 */
void MAMACALLTYPE
nngBridgeMamaTransportImpl_queueCallback (mamaQueue queue, void* closure)
{

    mama_status           status          = MAMA_STATUS_OK;
    mamaMsg               tmpMsg          = NULL;
    msgBridge             bridgeMsg       = NULL;
    memoryPool*           pool            = NULL;
    memoryNode*           node            = (memoryNode*) closure;
    nngTransportMsg*      tmsg            = (nngTransportMsg*) node->mNodeBuffer;
    uint32_t              bufferSize      = (uint32_t)tmsg->mNodeSize;
    const void*           buffer          = tmsg->mNodeBuffer;
    const char*           subject         = (char*)buffer;
    nngSubscription*      subscription    = (nngSubscription*) tmsg->mSubscription;
    nngTransportBridge*   impl            = subscription->mTransport;
    nngQueueBridge*       queueImpl       = NULL;

    /* Can't do anything without a subscriber */
    if (NULL == subscription)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaTransportImpl_queueCallback(): "
                  "Called with NULL subscriber (destroyed?)");
        return;
    }

    if (0 == endpointPool_isRegistedByContent (impl->mSubEndpoints,
                                               subject,
                                               subscription))
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaTransportImpl_queueCallback(): "
                  "Subscriber has been unregistered since msg was enqueued.");
        return;
    }

    /* Make sure that the subscription is processing messages */
    if (1 != subscription->mIsNotMuted)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaTransportImpl_queueCallback(): "
                  "Skipping update - subscription %p is muted.", subscription);
        return;
    }

    /* This is the reuseable message stored on the associated MamaQueue */
    tmpMsg = mamaQueueImpl_getMsg (subscription->mMamaQueue);
    if (NULL == tmpMsg)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaTransportImpl_queueCallback(): "
                  "Could not get cached mamaMsg from event queue.");
        return;
    }

    /* Get the bridge message from the mamaMsg */
    status = mamaMsgImpl_getBridgeMsg (tmpMsg, &bridgeMsg);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaTransportImpl_queueCallback(): "
                  "Could not get bridge message from cached "
                  "queue mamaMsg [%s]", mamaStatus_stringForStatus (status));
        return;
    }
    /* Unpack this bridge message into a MAMA msg implementation */
    status = nngBridgeMamaMsgImpl_deserialize (bridgeMsg, buffer, bufferSize, tmpMsg);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaTransportImpl_queueCallback(): "
                  "nngBridgeMamaMsgImpl_unpack() failed. [%s]",
                  mamaStatus_stringForStatus (status));
    }
    else
    {
        /* Process the message as normal */
        status = mamaSubscription_processMsg (subscription->mMamaSubscription,
                                              tmpMsg);
        if (MAMA_STATUS_OK != status)
        {
            mama_log (MAMA_LOG_LEVEL_ERROR,
                      "nngBridgeMamaTransportImpl_queueCallback(): "
                      "mamaSubscription_processMsg() failed. [%d]", status);
        }
    }

    mamaQueue_getNativeHandle (queue, (void**)&queueImpl);

    // Return the memory node to the pool
    pool = (memoryPool*) baseBridgeMamaQueueImpl_getClosure ((queueBridge) queueImpl);
    memoryPool_returnNode (pool, node);

    return;
}

const char* nngBridgeMamaTransportImpl_getParameterWithVaList (
                                            char*       defaultVal,
                                            char*       paramName,
                                            const char* format,
                                            va_list     arguments)
{
    const char* property = NULL;

    /* Create the complete transport property string */
    vsnprintf (paramName, PARAM_NAME_MAX_LENGTH,
               format, arguments);

    /* Get the property out for analysis */
    property = properties_Get (mamaInternal_getProperties (),
                               paramName);
    /* Properties will return NULL if parameter is not specified in configs */
    if (property == NULL)
    {
        property = defaultVal;
    }

    return property;
}

const char* nngBridgeMamaTransportImpl_getParameter (
                                            const char* defaultVal,
                                            const char* format, ...)
{
    char        paramName[PARAM_NAME_MAX_LENGTH];
    const char* returnVal = NULL;
    /* Create list for storing the parameters passed in */
    va_list     arguments;

    /* Populate list with arguments passed in */
    va_start (arguments, format);

    returnVal = nngBridgeMamaTransportImpl_getParameterWithVaList (
                        (char*)defaultVal,
                        paramName,
                        format,
                        arguments);

    /* These will be equal if unchanged */
    if (returnVal == defaultVal)
    {
        mama_log (MAMA_LOG_LEVEL_FINER,
                  "nngBridgeMamaTransportImpl_getParameter: "
                  "parameter [%s]: [%s] (Default)",
                  paramName,
                  returnVal);
    }
    else
    {
        mama_log (MAMA_LOG_LEVEL_FINER,
                  "nngBridgeMamaTransportImpl_getParameter: "
                  "parameter [%s]: [%s] (User Defined)",
                  paramName,
                  returnVal);
    }

    /* Clean up the list */
    va_end(arguments);

    return returnVal;
}

void* nngBridgeMamaTransportImpl_dispatchThread (void* closure)
{
    nngTransportBridge*     impl          = (nngTransportBridge*)closure;
    const char*             subject       = NULL;
    endpoint_t*             subs          = NULL;
    size_t                  subCount      = 0;
    size_t                  subInc        = 0;
    mama_status             status        = MAMA_STATUS_OK;
    nngSubscription*        subscription  = NULL;

    /*
     * Check if we should be still dispatching.
     * We shouldn't need to lock around this, as we're performing a simple value
     * read - if it changes in the middle of the read, we don't actually care.
     */
    while (1 == impl->mIsDispatching)
    {
        int      rv;
        nng_msg *nmsg;
        mama_log(MAMA_LOG_LEVEL_ERROR, "mIsDispatching = [%u]; Receiving message", impl->mIsDispatching);
        rv = nng_recvmsg(impl->mNngSocketSubscriber, &nmsg, 0);
        mama_log(MAMA_LOG_LEVEL_ERROR, "mIsDispatching = [%u]; Received... something", impl->mIsDispatching);
        switch (rv) {
            case NNG_ETIMEDOUT:
            case NNG_ESTATE:
                // Either a regular timeout, or we reached the
                // end of an event like a survey completing.
                continue;
            case 0:
                mama_log(MAMA_LOG_LEVEL_FINEST, "Received message [%s]", nng_strerror(rv));
                break;
            default:
                mama_log(MAMA_LOG_LEVEL_ERROR, "Receive error: %s", nng_strerror(rv));
                continue;
        }

        void* data = nng_msg_body(nmsg);
        size_t len = nng_msg_len(nmsg);
        // We just received a message if we got this far
        subject = (char*) data;

        status = endpointPool_getRegistered (impl->mSubEndpoints,
                                             subject,
                                             &subs,
                                             &subCount);

        if (MAMA_STATUS_OK != status)
        {
            mama_log (MAMA_LOG_LEVEL_ERROR,
                      "nngBridgeMamaTransportImpl_dispatchThread(): "
                      "Could not query registration table "
                      "for symbol %s (%s)",
                      subject,
                      mamaStatus_stringForStatus (status));

            continue;
        }

        if (0 == subCount)
        {
            mama_log (MAMA_LOG_LEVEL_FINEST,
                      "nngBridgeMamaTransportImpl_dispatchThread(): "
                      "discarding uninteresting message "
                      "for symbol %s", subject);

            continue;
        }

        for (subInc = 0; subInc < subCount; subInc++)
        {
            queueBridge      queueImpl = NULL;
            memoryPool*      pool      = NULL;
            memoryNode*      node      = NULL;
            nngTransportMsg* tmsg      = NULL;

            subscription = (nngSubscription*)subs[subInc];
            if (1 == subscription->mIsTportDisconnected)
            {
                subscription->mIsTportDisconnected = 0;
            }

            if (1 != subscription->mIsNotMuted)
            {
                mama_log (MAMA_LOG_LEVEL_FINEST,
                          "nngBridgeMamaTransportImpl_dispatchThread(): "
                          "muted - not queueing update for symbol %s",
                          subject);
                continue;
            }
            queueImpl = (queueBridge) subscription->mNngQueue;

            // Get the memory pool from the queue, creating if necessary
            pool = (memoryPool*) baseBridgeMamaQueueImpl_getClosure (queueImpl);
            if (NULL == pool)
            {
                pool = memoryPool_create (impl->mMemoryPoolSize, impl->mMemoryNodeSize);
                baseBridgeMamaQueueImpl_setClosure (queueImpl, pool,
                        nngBridgeMamaTransportImpl_queueClosureCleanupCb);
            }

            // allocate/populate nngTransportMsg
            node = memoryPool_getNode (pool, sizeof(nngTransportMsg) + len);
            tmsg = (nngTransportMsg*) node->mNodeBuffer;
            tmsg->mNodeBuffer   = (uint8_t*)(tmsg + 1);
            tmsg->mNodeSize     = len;
            tmsg->mSubscription = subscription;
            memcpy (tmsg->mNodeBuffer, data,tmsg->mNodeSize);

            // callback (queued) will release the message
            baseBridgeMamaQueue_enqueueEvent ((queueBridge) queueImpl,
                    nngBridgeMamaTransportImpl_queueCallback, node);
        }
    }

    impl->mOmnngDispatchStatus = MAMA_STATUS_OK;
    return NULL;
}

void MAMACALLTYPE
nngBridgeMamaTransportImpl_queueClosureCleanupCb (void* closure)
{
    memoryPool* pool = (memoryPool*) closure;
    if (NULL != pool)
    {
        mama_log (MAMA_LOG_LEVEL_FINE,
                  "Destroying memory pool for queue %p.", closure);
        memoryPool_destroy (pool, NULL);
    }
}
