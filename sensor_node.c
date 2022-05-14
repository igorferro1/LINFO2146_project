#include "contiki.h"
#include "net/netstack.h"
#include "random.h"
#include "net/packetbuf.h"
#include "dev/leds.h"
#include "net/nullnet/nullnet.h"
#include "net/linkaddr.h"

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define ALL_LEDS LEDS_RED | LEDS_YELLOW | LEDS_GREEN

#include <string.h>
#include <stdio.h> /* For printf() */

#if MAC_CONF_WITH_TSCH
#include "net/mac/tsch/tsch.h"
static linkaddr_t coordinator_addr = {{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
#endif /* MAC_CONF_WITH_TSCH */

/*---------------------------------------------------------------------------*/
PROCESS(process_unic, "unicast");
PROCESS(process_broad, "broadcast");
AUTOSTART_PROCESSES(&process_broad, &process_unic);

/**
 * @brief Template of message that will be sent/received by the node
 */

static int addrVal[2] = {0, 0};
static linkaddr_t parent_addr; // = {{(addrVal[0]), (addrVal[1])}};
static int rank = 255;
static int rssi_parent = -1000;

struct message
{
    int mType;      /* 0: Announcement: "I can be your parent"
                       1: Node down: "I lost my parent"
                       2: Data message from node to server
                       3: Open valve message, from server to node */
    int broadOrUni; /* 0: broadcast
                       1: unicast */
    int rankSender;
    int addr_dest_opening[2]; /* Used on data message and open valve message,
                                 because the server has to know which one is
                                 being calculated to order which one is going to open */
    int valueRead;            /* value read and sent to the server */
};

// /**
//  * @brief Routing table entry. Each node contains a routing table that
//  * tells where to forward the data
//  */
struct routingEntry
{
    int target[2];   // final node
    int nextJump[2]; // next node
    int alive;       // if this connection is alive
};

static struct routingEntry routingTable[64];
static int routingTSize = sizeof(routingTable) / sizeof(routingTable[0]);

struct message rcv_msg;

static void create_packet(int broadUni, int mesType, int rankS, int addrSend[2], int valueR)
{
    static uint8_t buf[6];
    buf[0] = broadUni;
    buf[1] = mesType;
    buf[2] = rankS;
    buf[3] = addrSend[0];
    buf[4] = addrSend[1];
    buf[5] = valueR;
    LOG_INFO("Came to create the message of type %d, rank %d", buf[1], buf[2]);
    LOG_INFO_("\n");
    nullnet_buf = (uint8_t *)&buf;
    nullnet_len = sizeof(buf);
}

/*---------------------------------------------------------------------------
 * Function that will be called when the node receives any data
 *
 */

void rcv_broadcast(struct message rcv_msg, const linkaddr_t *src);
void rcv_unicast(struct message rcv_msg, const linkaddr_t *src);

void input_callback(const void *data, uint16_t len,
                    const linkaddr_t *src, const linkaddr_t *dest)
{
    static uint8_t bufRcv[6];

    // process_post(&process_broad, PROCESS_EVENT_MSG, "parentDown");
    if (len == sizeof(bufRcv))
    {

        memcpy(bufRcv, data, sizeof(bufRcv)); /* Copies the received message to the memory */

        (&rcv_msg)->broadOrUni = bufRcv[0];
        (&rcv_msg)->mType = bufRcv[1];
        (&rcv_msg)->rankSender = bufRcv[2];
        (&rcv_msg)->addr_dest_opening[0] = bufRcv[3];
        (&rcv_msg)->addr_dest_opening[1] = bufRcv[4];
        (&rcv_msg)->valueRead = bufRcv[5];

        if (rcv_msg.broadOrUni == 0)
            rcv_broadcast(rcv_msg, src);
        else
        {
            if (((&rcv_msg)->mType == 2 && (&rcv_msg)->rankSender == rank + 1) || ((&rcv_msg)->mType == 3 && (&rcv_msg)->rankSender == rank - 1))
            {
                rcv_unicast(rcv_msg, src);
            }
        }
    }
}

/* Types of messages to deal with:
    - Sending broadcasts: advertise/down
    - Receiving broadcasts: from advertisements/down
    - Receiving unicasts/runicasts: from nodes forwarding data (par -> chi or chi -> par), or command to that specific node
    - Sending unicasts/runicasts: to parents for data

*/

/* General logic for the sensor node to send data
    - Initialize, find a parent
    - wait one minute
    - send random value to parent

 */

void rcv_broadcast(struct message rcv_msg, const linkaddr_t *src)
{
    LOG_INFO("Passou pro broadcast");
    LOG_INFO_("\n");
    /* if it's a message of "i can be your parent"
       possible scenarios:
        - the node that sent the broadcast has a lower rank as the current parent: compare rssis and change parents if the new is better
        - the node that sent the broadcast has the same rank as the current parent: compare rssis and change parent if the new is better
        - the node that sent the broadcast has a higher rank: keep parent
        */
    if (rcv_msg.mType == 0 && rcv_msg.rankSender < rank) /* "i can be your parent" message */
        if (rcv_msg.rankSender <= rank - 1)
        {
            if (rssi_parent < packetbuf_attr(PACKETBUF_ATTR_RSSI))
            { /* if it's better, it assumes the position of parent */
                LOG_INFO("vai assumir");
                LOG_INFO_("\n");
                addrVal[0] = src->u8[0];
                addrVal[1] = src->u8[1];
                parent_addr.u8[0] = src->u8[0];
                parent_addr.u8[1] = src->u8[1];
                // linkaddr_set_node_addr(&parent_addr);
                rssi_parent = packetbuf_attr(PACKETBUF_ATTR_RSSI);
                rank = rcv_msg.rankSender + 1;
                process_post(&process_broad, PROCESS_EVENT_MSG, "announce");
            }
        }
    /* if it's a message of "i'm down!" and the node down is the parent of the node
        - we have to reset the parameters and try to find another parent*/
    if (rcv_msg.mType == 1 && addrVal[0] == src->u8[0] && addrVal[1] == src->u8[1])
    {
        if (rank != 2) /* only reset if it's not connected directly to root */
        {
            rank = 255;
            addrVal[0] = 0;
            addrVal[1] = 1;
            parent_addr.u8[0] = 0;
            parent_addr.u8[1] = 1;
            rssi_parent = -1000;
            int i;
            for (i = 0; i < routingTSize; i = i + 1)
                routingTable[i].alive = 0;
        }
        process_post(&process_broad, PROCESS_EVENT_MSG, "parentDown");
    }
    if (rcv_msg.mType == 1 && rcv_msg.rankSender > rank)
    {
        /* if a node lost his parent and the node is more down the tree
            then we announce to reconnect that node */
        process_post(&process_broad, PROCESS_EVENT_MSG, "announce");
    }
}

void rcv_unicast(struct message rcv_msg, const linkaddr_t *src)
{
    /* if it's data to send to the server:
        - we could add the sending node to the routing table, connecting by the data send
    */
    if (rcv_msg.mType == 2)
    {
        LOG_INFO("Received unicast");
        LOG_INFO_("\n");
        int presentInTable = 0;
        int i;
        for (i = 0; i < routingTSize; i = i + 1)
        {
            if (routingTable[i].target[0] == rcv_msg.addr_dest_opening[0] && routingTable[i].target[1] == rcv_msg.addr_dest_opening[1] && routingTable[i].alive)
            {
                // if the node that sent is present on the table, we just updated from where it came from
                routingTable[i].nextJump[0] = src->u8[0];
                routingTable[i].nextJump[1] = src->u8[1];
                presentInTable = 1;
                break;
            }
        }
        if (!presentInTable)
        {
            static struct routingEntry entry;
            entry.alive = 1;
            entry.target[0] = rcv_msg.addr_dest_opening[0];
            entry.target[1] = rcv_msg.addr_dest_opening[1];
            entry.nextJump[0] = src->u8[0];
            entry.nextJump[1] = src->u8[1];

            for (i = 0; i < routingTSize; i = i + 1)
            {
                if (!(routingTable[i].alive))
                {
                    routingTable[i] = entry;
                    break;
                }
            }

        } /* add to routing table and forward it upwards */
        process_post(&process_unic, PROCESS_EVENT_MSG, "forwardData");
    }

    /* if it's just an open valve message:
        - check if it's for this node
            - if it's for this node, then open
            - otherwise look in the routing table and send the message to the next node */
    else if (rcv_msg.mType == 3)
    {
        LOG_INFO("Received command to open");
        LOG_INFO_("\n");
        if (rcv_msg.addr_dest_opening[0] == linkaddr_node_addr.u8[0] && rcv_msg.addr_dest_opening[1] == linkaddr_node_addr.u8[1])
        {
            LOG_INFO("Opening");
            LOG_INFO_("\n");
            process_post(&process_unic, PROCESS_EVENT_MSG, "openValve");
        }
        else
        {
            LOG_INFO("Passing command to open");
            LOG_INFO_("\n");
            process_post(&process_unic, PROCESS_EVENT_MSG, "forwardCommand");
            /* find the next node in the routing table and send the data to him */
        }
    }
}

PROCESS_THREAD(process_broad, ev, data)
{
    static struct etimer timerBroad;
    // static uint8_t bufRcv[6];
    int addrNode[2];
    addrNode[0] = linkaddr_node_addr.u8[0];
    addrNode[1] = linkaddr_node_addr.u8[1];

    PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
    tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

    // setup and alive message
    nullnet_set_input_callback(input_callback);
    NETSTACK_NETWORK.output(NULL);

    while (1)
    {

        etimer_set(&timerBroad, CLOCK_SECOND * 31);
        PROCESS_WAIT_EVENT();

        if (etimer_expired(&timerBroad))
        {
            LOG_INFO("Rank %d announcing...", rank);
            LOG_INFO_("\n");
            create_packet(0, 0, rank, addrNode, 0); // value doesn't matter here
            NETSTACK_NETWORK.output(NULL);
        }
        else if (!strcmp(data, "announce"))
        {
            LOG_INFO("node rank %d received broad and will announce", rank);
            LOG_INFO_("\n");
            create_packet(0, 0, rank, addrNode, 0); // value doesn't matter here
            NETSTACK_NETWORK.output(NULL);
        }
        else if (!strcmp(data, "parentDown"))
        {
            create_packet(0, 1, rank, addrNode, 0); /* announce that it's down as well */
            NETSTACK_NETWORK.output(NULL);
        }
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(process_unic, ev, data)
{
    static struct etimer timerValve, sendData, forwardData;
    // static uint8_t bufRcv[6];
    int addrNode[2];
    addrNode[0] = linkaddr_node_addr.u8[0];
    addrNode[1] = linkaddr_node_addr.u8[1];

    PROCESS_BEGIN();

    leds_off(LEDS_ALL);
#if MAC_CONF_WITH_TSCH
    tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

    // setup and alive message
    nullnet_set_input_callback(input_callback);
    NETSTACK_NETWORK.output(NULL);

    while (1)
    {

        etimer_set(&sendData, CLOCK_SECOND * 60);
        PROCESS_WAIT_EVENT();

        if (etimer_expired(&sendData))
        {
            int readValue = random_rand();

            LOG_INFO("Node %d sending %d as data", rank, readValue); // change data generation method
            LOG_INFO_("\n");

            etimer_set(&sendData, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&sendData));

            create_packet(1, 2, rank, addrNode, 254); // random_rand()); /* send the data */
            linkaddr_t parentData = {{addrVal[0], addrVal[1], 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
            NETSTACK_NETWORK.output(&parentData);
            LOG_INFO("Send data from itself");
            LOG_INFO_("\n");
        }
        else if (!strcmp(data, "forwardData"))
        {
            LOG_INFO("Node rank %d will forward the data %d that received to parent %d %d", rank, rcv_msg.valueRead, (&parent_addr)->u8[0], (&parent_addr)->u8[1]);
            LOG_INFO_("\n");

            etimer_set(&forwardData, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&forwardData));

            create_packet(1, 2, rank, rcv_msg.addr_dest_opening, rcv_msg.valueRead);
            linkaddr_t parentData = {{addrVal[0], addrVal[1], 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
            NETSTACK_NETWORK.output(&parentData);
            LOG_INFO("Send data from other node");
            LOG_INFO_("\n");
        }
        else if (!strcmp(data, "forwardCommand"))
        {
            LOG_INFO("Will pass the command to open");
            LOG_INFO_("\n");
            int presentInTable = 0;
            int i;

            create_packet(1, 3, rank, rcv_msg.addr_dest_opening, rcv_msg.valueRead);

            for (i = 0; i < routingTSize; i = i + 1)
            {
                if (routingTable[i].target[0] == rcv_msg.addr_dest_opening[0] && routingTable[i].target[1] == rcv_msg.addr_dest_opening[1] && routingTable[i].alive)
                {
                    // if the node that sent is present on the table, we just updated from where it came from
                    LOG_INFO("Target %d %d, next jump %d %d", routingTable[i].target[0], routingTable[i].target[1], routingTable[i].nextJump[0], routingTable[i].nextJump[1]);
                    LOG_INFO_("\n");
                    linkaddr_t destinationCommand = {{routingTable[i].nextJump[0], routingTable[i].nextJump[1], 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
                    NETSTACK_NETWORK.output(&destinationCommand);

                    presentInTable = 1;
                    break;
                }
            }

            if (!presentInTable)
            {

                NETSTACK_NETWORK.output(NULL);
            }
        }
        else if (!strcmp(data, "openValve"))
        {
            leds_on(ALL_LEDS);
            etimer_set(&timerValve, CLOCK_SECOND * 600);
        }
        else if (etimer_expired(&timerValve))
            leds_off(ALL_LEDS);
    }

    PROCESS_END();
}
