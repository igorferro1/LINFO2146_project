#include "contiki.h"
#include "net/netstack.h"
#include "random.h"
#include "net/packetbuf.h"
#include "net/nullnet/nullnet.h"
#include "net/linkaddr.h"

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#include <string.h>
#include <stdio.h> /* For printf() */

#if MAC_CONF_WITH_TSCH
#include "net/mac/tsch/tsch.h"
static linkaddr_t coordinator_addr = {{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
#endif /* MAC_CONF_WITH_TSCH */

/*---------------------------------------------------------------------------*/
// PROCESS(process_uni, "unicast");
PROCESS(process_broad, "broadcast");
AUTOSTART_PROCESSES(&process_broad);

/**
 * @brief Template of message that will be sent/received by the node
 */

static int parent_addr[2];
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
    int target;
    int nextJump[2];
    int alive;
};

static struct routingEntry routingTable[64]; // static = init to 0 for all value
static int routingTSize = sizeof(routingTable) / sizeof(routingTable[0]);

struct message rcv_msg;

static void create_packet(int broadUni, int mesType, int rankS, int addrSend[2], int valueR)
{
    uint8_t buf[6];
    buf[0] = broadUni;
    buf[1] = mesType;
    buf[2] = rankS;
    buf[3] = addrSend[0];
    buf[4] = addrSend[1];
    buf[5] = valueR;
    LOG_INFO("Came to create the message");
    LOG_INFO_("\n");
    // new_message->broadOrUni = broadUni;
    // new_message->mType = mesType;
    // new_message->rankSender = rankS;
    // new_message->addr_dest_opening[0] = addrSend[0];
    // new_message->addr_dest_opening[1] = addrSend[1];
    // new_message->valueRead = valueR;
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
    uint8_t bufRcv[6];

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

        LOG_INFO("Got message in node rank %d", rank);
        LOG_INFO("\n");

        if (rcv_msg.broadOrUni == 0)
        {
            LOG_INFO("Entrou no broaduni");
            LOG_INFO_("\n");
            rcv_broadcast(rcv_msg, src);
        }
        // else
        //     rcv_unicast(rcv_msg, src);
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
            // LOG_INFO("Entrou no i can be parent");
            // LOG_INFO_("\n");
            // LOG_INFO("cur: %d, %d", parent_addr[0], parent_addr[1]);
            // LOG_INFO_("\n");
            // LOG_INFO("new par: %d, %d", src->u8[0], src->u8[1]);
            // LOG_INFO_("\n");
            // LOG_INFO("RSSI CUR NEW: %d, %d", rssi_parent, packetbuf_attr(PACKETBUF_ATTR_RSSI));
            // LOG_INFO_("\n");
            if ((src->u8[0] != parent_addr[0] || src->u8[1] != parent_addr[1]) && rssi_parent < packetbuf_attr(PACKETBUF_ATTR_RSSI))
            { /* if it's better, it assumes the position of parent */
                // LOG_INFO("vai assumir");
                // LOG_INFO_("\n");
                parent_addr[0] = src->u8[0];
                parent_addr[1] = src->u8[1];
                rssi_parent = packetbuf_attr(PACKETBUF_ATTR_RSSI);
                rank = rcv_msg.rankSender + 1;
                process_post(&process_broad, PROCESS_EVENT_MSG, "announce");
            }
        }
    /* if it's a message of "i'm down!" and the node down is the parent of the node
        - we have to reset the parameters and try to find another parent*/
    if (rcv_msg.mType == 1 && parent_addr[0] == src->u8[0] && parent_addr[1] == src->u8[1])
    {
        rank = 255;
        parent_addr[0] = 0;
        parent_addr[1] = 1;
        rssi_parent = -1000;
        process_post(&process_broad, PROCESS_EVENT_MSG, "parentDown");
        int i;
        for (i = 0; i < routingTSize; i = i + 1)
            routingTable[i].alive = 0;
        /* clean routing table maybe */
    }
    if (rcv_msg.mType == 1 && rcv_msg.rankSender > rank)
    {
        /* if a node lost his parent and the node is more down the tree
            then we announce to reconnect that node */
        process_post(&process_broad, PROCESS_EVENT_MSG, "announce");
    }
}

// void rcv_unicast(struct message rcv_msg, const linkaddr_t *src)
// {
//     /* if it's data to send to the server:
//         - we could add the sending node to the routing table, connecting by the data send
//     */
//     if (rcv_msg.mType == 2)
//     {
//         /* add to routing table and forward it upwards? */
//     }

//     /* if it's just an open valve message:
//         - check if it's for this node
//             - if it's for this node, then open
//             - otherwise look in the routing table and send the message to the next node */
//     else if (rcv_msg.mType == 3)
//     {
//         if (rcv_msg.addr_dest_opening[0] == linkaddr_node_addr.u8[0] && rcv_msg.addr_dest_opening[1] == linkaddr_node_addr.u8[1])
//         {
//             /* open valve */
//         }
//         else
//         {
//             /* find the next node in the routing table and send the data to him */
//         }
//     }
// }
/*---------------------------------------------------------------------------
    PROCESS FOR BROADCAST
*/
// PROCESS_THREAD(process_broad, ev, data)
// {
//     static struct etimer periodic_timer;
//     static unsigned count = 0;

//     PROCESS_BEGIN();

// #if MAC_CONF_WITH_TSCH
//     tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
// #endif /* MAC_CONF_WITH_TSCH */

//     /* Initialize NullNet */
//     nullnet_buf = (uint8_t *)&count;
//     nullnet_len = sizeof(count);
//     nullnet_set_input_callback(input_callback);

//     etimer_set(&periodic_timer, 8 * (CLOCK_SECOND));
//     while (1)
//     {
//         PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
//         LOG_INFO("Sending %u to ", count);
//         LOG_INFO_LLADDR(NULL);
//         LOG_INFO_("\n");

//         memcpy(nullnet_buf, &count, sizeof(count));
//         nullnet_len = sizeof(count);

//         NETSTACK_NETWORK.output(NULL);
//         count++;
//         etimer_reset(&periodic_timer);
//     }

//     PROCESS_END();
// }
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

        etimer_set(&timerBroad, CLOCK_SECOND * 30);
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
            etimer_set(&timerBroad, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timerBroad));
            create_packet(0, 0, rank, addrNode, 0); // value doesn't matter here
            NETSTACK_NETWORK.output(NULL);
        }
        else if (!strcmp(data, "parentDown"))
        {
            etimer_set(&timerBroad, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timerBroad));
            create_packet(0, 1, rank, addrNode, 0); /* announce that it's down as well */
            NETSTACK_NETWORK.output(NULL);
        }
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/

/* Function that generate random value as a reading of the sensor */

/* TODO: See how to do the routing from scratch (follow the examples that they gave and check the TPs again)
   At first I thought we could use Rime, but apparently not
*/
