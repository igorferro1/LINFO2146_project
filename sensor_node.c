#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"

#include <string.h>
#include <stdio.h> /* For printf() */
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_unicast, "unicast");
AUTOSTART_PROCESSES(&nullnet_example_process);

/**
 * @brief Template of message that will be sent/received by the node
 */
struct message
{
    int mType;      /* data, find parents, etc */
    int broadOrUni; /* broadcast or unicast */
    int rankSender;
    char data[32];
};

/**
 * @brief Routing table entry. Each node contains a routing table that
 * tells where to forward the data
 */
struct routingEntry
{
    int target;
    int nextJump;
};

static struct routingEntry routingTable[64]; // static = init to 0 for all value
static int route_table_len = sizeof(route_table) / sizeof(route_table[0]);

struct message rcv_msg;
/* idk if those are necessary
struct message broad_rcv;
struct message uni_rcv;
*/

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
                rcv_broadcast(rcv_msg);
        else rcv_unicast(rcv_msg);
    }
}

void rcv_broadcast(struct message rcv_msg)
{
    /*  */
}

void rcv_unicast(struct message rcv_msg)
{
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

/* Types of messages to deal with:
    - Sending broadcasts: find parent
    - Receiving broadcasts: from nodes searching for parents
    - Receiving unicasts/runicasts: from nodes forwarding data (par -> chi or chi -> par), or command to that specific node
    - Sending unicasts/runicasts: to parents for data

*/

/* General logic for the sensor node to send data
    - Initialize, find a parent
    - wait one minute
    - send random value to parent

 */
