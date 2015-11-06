// Copyright 2015 Apcera Inc. All rights reserved.

#include "examples.h"

static volatile int64_t count   = 0;
static int64_t          total   = 0;
static int64_t          start   = 0;
static volatile int64_t elapsed = 0;
static bool             print   = false;

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    // If 'print' is on, the server is likely to break the connection
    // since the client library will become a slow consumer.
    if (print)
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

    if (start == 0)
        start = nats_Now();

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    if (++count == total)
        elapsed = nats_Now() - start;

    natsMsg_Destroy(msg);
}

static void
asyncCb(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure)
{
    printf("Async error: %d - %s\n", err, natsStatus_GetText(err));
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsStatistics      *stats = NULL;
    natsMsg             *msg   = NULL;
    bool                async  = true;
    const char          *subj  = NULL;
    const char          *name  = NULL;
    natsStatus          s;

    if (argc != 5)
    {
        printf("Usage: %s <mode:async|sync> <name> <subject> <count>\n", argv[0]);
        exit(1);
    }

    async = (strcasecmp(argv[1], "async") == 0);
    name  = argv[2];
    subj  = argv[3];
    total = atol(argv[4]);
    printf("Listening %ssynchronously on '%s' with name '%s'.\n",
           (async ? "a" : ""), subj, name);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, NATS_DEFAULT_URL);
    if ((s == NATS_OK) && async)
        s = natsOptions_SetErrorHandler(opts, asyncCb, NULL);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
    {
        if (async)
            s = natsConnection_QueueSubscribe(&sub, conn, subj, name, onMsg, NULL);
        else
            s = natsConnection_QueueSubscribeSync(&sub, conn, subj, name);
    }
    if (s == NATS_OK)
        s = natsSubscription_AutoUnsubscribe(sub, (int) total);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if ((s == NATS_OK) && async)
    {
        while (s == NATS_OK)
        {
            s = printStats(STATS_IN|STATS_COUNT,conn, sub, stats, count, 0);

            if (count == total)
                break;

            if (s == NATS_OK)
                nats_Sleep(1000);
        }
    }
    else if (s == NATS_OK)
    {
        int64_t last = 0;

        for (count = 0; (s == NATS_OK) && (count < total); count++)
        {
            s = natsSubscription_NextMsg(&msg, sub, 10000);
            if (s != NATS_OK)
                break;

            if (start == 0)
                start = nats_Now();

            if (nats_Now() - last >= 1000)
            {
                s = printStats(STATS_IN|STATS_COUNT,conn, sub, stats, count, 0);
                last = nats_Now();
            }

            natsMsg_Destroy(msg);
        }
    }

    if (s == NATS_OK)
    {
        printPerf("Received", total, start, elapsed);
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
    }

    // Destroy all our objects to avoid report of memory leak
    natsStatistics_Destroy(stats);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
