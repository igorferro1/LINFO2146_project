#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "cc2420.h" // signal strength

#include <string.h>
#include <stdio.h> /* For printf() */
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_unicast, "unicast");
AUTOSTART_PROCESSES(&nullnet_example_process);

/**
 * @brief Template of message that will be sent/received by the node
 */

static int parent_addr[2];
static int rank = 1000;
static int rssi_parent = -1000;

struct message
{
    int mType;      /* data, find parents, etc */
    int broadOrUni; /* broadcast or unicast */
    int rankSender;
    int addr_sender[2];
    int addr_dest_opening[2]; /* to open valve we need the destination address */
    char data[32];
};

/**
 * @brief Routing table entry. Each node contains a routing table that
 * tells where to forward the data
 */
struct routingEntry
{
    int target;
    int nextJump[2];
};

static struct routingEntry routingTable[64]; // static = init to 0 for all value
static int route_table_len = sizeof(route_table) / sizeof(route_table[0]);

struct message rcv_msg;

/*---------------------------------------------------------------------------
 * Function that will be called when the node receives any data
 *
 */

void input_callback(const void *data, uint16_t len,
                    const linkaddr_t *src, const linkaddr_t *dest)
{
    if (len == sizeof(struct message))
    {
        memcpy(&rcv_msg, data, sizeof(struct message)); /* Copies the received message to the memory */
        print("Got message in node x (will be replaced with the rank)")

            if (rcv_msg.broadOrUni == 0)
                rcv_broadcast(rcv_msg, &src);
        else rcv_unicast(rcv_msg, &src);
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
    /* if it's a message of "i can be your parent"
       possible scenarios:
        - the node that sent the broadcast has a lower rank as the current parent: compare rssis and change parents if the new is better
        - the node that sent the broadcast has the same rank as the current parent: compare rssis and change parent if the new is better
        - the node that sent the broadcast has a higher rank: keep parent
        */
    if (rcv_msg.mType == 0 && rcv_msg.rankSender < rank) /* "i can be your parent" message */
        if (rcv_msg.rankSender <= rank - 1)
        {
            if ((src->u8[0] != parent_addr[0] || src->u8[1] != parent_addr[1]) && rssi_parent > cc2420_last_rssi)
            { /* if it's better, it assumes the position of parent */
                parent_addr[0] = src->u8[0];
                parent_addr[1] = src->u8[1];
                rssi_parent = cc2420_last_rssi;
                rank = rcv_msg.rankSender + 1;
                /* advertise to next ones maybe? */
            }
            /* if it's the same parent, advertise to the next ones maybe? */
        }
    /* if it's a message of "i'm down!" and the node down is the parent of the node
        - we have to reset the parameters and try to find another parent*/
    if (rcv_msg.mType == 1 && parent_addr[0] == src->u8[0] && parent_addr[1] == src->u8[1])
    {
        rank = 1000;
        parent_addr[0] = 0;
        parent_addr[1] = 1;
        rssi_parent = -1000;
        /* tell other nodes about it and try to find a parent */
        /* clean routing table maybe */
    }
}

void rcv_unicast(struct message rcv_msg, const linkaddr_t *src)
{
    /* if it's data to send to the server:
        - we could add the sending node to the routing table, connecting by the data send
    */
    if (rcv_msg.mType == 2)
    {
        /* add to routing table and forward it upwards? */
    }

    /* if it's just an open valve message:
        - check if it's for this node
            - if it's for this node, then open
            - otherwise look in the routing table and send the message to the next node */
    else if (rcv_msg.mType == 3)
    {
    }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(nullnet_example_process, ev, data)
{
    static struct etimer periodic_timer;
    static unsigned count = 0;

    PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
    tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

    /* Initialize NullNet */
    nullnet_buf = (uint8_t *)&count;
    nullnet_len = sizeof(count);
    nullnet_set_input_callback(input_callback);

    if (!linkaddr_cmp(&dest_addr, &linkaddr_node_addr))
    {
        etimer_set(&periodic_timer, SEND_INTERVAL);
        while (1)
        {
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
            LOG_INFO("Sending %u to ", count);
            LOG_INFO_LLADDR(&dest_addr);
            LOG_INFO_("\n");

            NETSTACK_NETWORK.output(&dest_addr);
            count++;
            etimer_reset(&periodic_timer);
        }
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/

/* Function that generate random value as a reading of the sensor */

int randomValue()
{
    return rand(); // Maybe the function is not necessary lol
}

/* TODO: See how to do the routing from scratch (follow the examples that they gave and check the TPs again)
   At first I thought we could use Rime, but apparently not
*/
