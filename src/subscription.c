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
#include <string.h>
#include <mama/mama.h>
#include <mama/integration/subscription.h>
#include <mama/integration/transport.h>
#include <mama/integration/msg.h>
#include <mama/integration/queue.h>
#include <wombat/queue.h>
#include "transport.h"
#include "nng.h"
#include "subscription.h"
#include <mama/integration/endpointpool.h>
#include "nngbridgefunctions.h"
#include <errno.h>
#include <mama/integration/bridge/base.h>
#include <nng/protocol/pubsub0/sub.h>


/*=========================================================================
  =               Public interface implementation functions               =
  =========================================================================*/

mama_status
nngBridgeMamaSubscription_create (subscriptionBridge* subscriber,
                                  const char*         source,
                                  const char*         symbol,
                                  mamaTransport       tport,
                                  mamaQueue           queue,
                                  mamaMsgCallbacks    callback,
                                  mamaSubscription    subscription,
                                  void*               closure)
{
    nngSubscription*       impl        = NULL;
    nngTransportBridge*    transport   = NULL;
    mama_status            status      = MAMA_STATUS_OK;

    if ( NULL == subscriber || NULL == subscription || NULL == tport )
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                "nngBridgeMamaSubscription_create(): something NULL");
        return MAMA_STATUS_NULL_ARG;
    }

    status = mamaTransport_getBridgeTransport (tport,
                                               (transportBridge*) &transport);

    if (MAMA_STATUS_OK != status || NULL == transport)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                "nngBridgeMamaSubscription_create(): something NULL");
        return MAMA_STATUS_NULL_ARG;
    }

    /* Allocate memory for nng subscription implementation */
    impl = (nngSubscription*) calloc (1, sizeof (nngSubscription));
    if (NULL == impl)
    {
        return MAMA_STATUS_NOMEM;
    }

    mamaQueue_getNativeHandle(queue, &impl->mNngQueue);
    impl->mMamaCallback        = callback;
    impl->mMamaSubscription    = subscription;
    impl->mMamaQueue           = queue;
    impl->mTransport           = transport;
    impl->mSymbol              = symbol;
    impl->mClosure             = closure;
    impl->mIsNotMuted          = 1;
    impl->mIsTportDisconnected = 1;
    impl->mSubjectKey          = NULL;

    /* Use a standard centralized method to determine a topic key */
    nngBridgeMamaSubscriptionImpl_generateSubjectKey (NULL,
                                                      source,
                                                      symbol,
                                                      &impl->mSubjectKey);

    /* Register the endpoint */
    endpointPool_registerWithoutIdentifier (transport->mSubEndpoints,
                                            impl->mSubjectKey,
                                            &impl->mEndpointIdentifier,
                                            impl);

    /* Set the message meta data to reflect a subscription request */
    baseBridgeMamaMsgImpl_setMsgType (transport->mMsg,
                                     BASE_MSG_SUB_REQUEST);

    /* subscribe to the topic */
    nng_setopt(transport->mNngSocketSubscriber, NNG_OPT_SUB_SUBSCRIBE, impl->mSubjectKey, strlen (impl->mSubjectKey));

    mama_log (MAMA_LOG_LEVEL_FINEST,
              "nngBridgeMamaSubscription_create(): "
              "created interest for %s.",
              impl->mSubjectKey);

    /* Mark this subscription as valid */
    impl->mIsValid = 1;

    *subscriber =  (subscriptionBridge) impl;

    return MAMA_STATUS_OK;
}

mama_status
nngBridgeMamaSubscription_mute (subscriptionBridge subscriber)
{
    nngSubscription* impl = (nngSubscription*) subscriber;

    if (NULL == impl)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    impl->mIsNotMuted = 0;

    return MAMA_STATUS_OK;
}

mama_status
nngBridgeMamaSubscription_destroy (subscriptionBridge subscriber)
{
    nngSubscription*             impl            = NULL;
    nngTransportBridge*          transportBridge = NULL;
    mamaSubscription             parent          = NULL;
    void*                        closure         = NULL;
    wombat_subscriptionDestroyCB destroyCb       = NULL;

    if (NULL == subscriber)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    impl            = (nngSubscription*) subscriber;
    parent          = impl->mMamaSubscription;
    closure         = impl->mClosure;
    destroyCb       = impl->mMamaCallback.onDestroy;
    transportBridge = impl->mTransport;

    /* Remove the subscription from the transport's subscription pool. */
    if (NULL != transportBridge && NULL != transportBridge->mSubEndpoints
        && NULL != impl->mSubjectKey)
    {
        endpointPool_unregister (transportBridge->mSubEndpoints,
                                 impl->mSubjectKey,
                                 impl->mEndpointIdentifier);
    }

    if (NULL != impl->mSubjectKey)
    {
        free (impl->mSubjectKey);
    }

    if (NULL != impl->mEndpointIdentifier)
    {
        free ((void*)impl->mEndpointIdentifier);
    }

    free (impl);

    /*
     * Invoke the subscription callback to inform that the bridge has been
     * destroyed.
     */
    if (NULL != destroyCb)
        (*(wombat_subscriptionDestroyCB)destroyCb)(parent, closure);

    return MAMA_STATUS_OK;
}

int
nngBridgeMamaSubscription_isValid (subscriptionBridge subscriber)
{
    nngSubscription* impl = (nngSubscription*) subscriber;

    if (NULL != impl)
    {
        return impl->mIsValid;
    }
    return 0;
}

int
nngBridgeMamaSubscription_isTportDisconnected (subscriptionBridge subscriber)
{
	nngSubscription* impl = (nngSubscription*) subscriber;
	if (NULL == impl)
	{
		return MAMA_STATUS_NULL_ARG;
	}
    return impl->mIsTportDisconnected;
}

mama_status
nngBridgeMamaSubscription_muteCurrentTopic (subscriptionBridge subscriber)
{
    /* As there is one topic per subscription, this can act as an alias */
    return nngBridgeMamaSubscription_mute (subscriber);
}


/*=========================================================================
  =                  Public implementation functions                      =
  =========================================================================*/

/*
 * Internal function to ensure that the topic names are always calculated
 * in a particular way
 */
mama_status
nngBridgeMamaSubscriptionImpl_generateSubjectKey (const char*  root,
                                                  const char*  source,
                                                  const char*  topic,
                                                  char**       keyTarget)
{
    char        subject[MAX_SUBJECT_LENGTH];
    char*       subjectPos     = subject;
    size_t      bytesRemaining = MAX_SUBJECT_LENGTH;
    size_t      written        = 0;

    if (NULL != root)
    {
        mama_log (MAMA_LOG_LEVEL_FINEST,
                  "nngBridgeMamaSubscriptionImpl_generateSubjectKey(): R.");
        written         = snprintf (subjectPos, bytesRemaining, "%s", root);
        subjectPos     += written;
        bytesRemaining -= written;
    }

    if (NULL != source)
    {
        mama_log (MAMA_LOG_LEVEL_FINEST,
                  "nngBridgeMamaSubscriptionImpl_generateSubjectKey(): S.");
        /* If these are not the first bytes, prepend with a period */
        if(subjectPos != subject)
        {
            written     = snprintf (subjectPos, bytesRemaining, ".%s", source);
        }
        else
        {
            written     = snprintf (subjectPos, bytesRemaining, "%s", source);
        }
        subjectPos     += written;
        bytesRemaining -= written;
    }

    if (NULL != topic)
    {
        mama_log (MAMA_LOG_LEVEL_FINEST,
                  "nngBridgeMamaSubscriptionImpl_generateSubjectKey(): T.");
        /* If these are not the first bytes, prepend with a period */
        if (subjectPos != subject)
        {
            snprintf (subjectPos, bytesRemaining, ".%s", topic);
        }
        else
        {
            snprintf (subjectPos, bytesRemaining, "%s", topic);
        }
    }

    /*
     * Allocate the memory for copying the string. Caller is responsible for
     * destroying.
     */
    *keyTarget = strdup (subject);
    if (NULL == *keyTarget)
    {
        return MAMA_STATUS_NOMEM;
    }
    else
    {
        return MAMA_STATUS_OK;
    }
}
