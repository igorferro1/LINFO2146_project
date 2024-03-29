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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dev/serial-line.h"
#include "cpu/msp430/dev/uart0.h"

PROCESS(process_broad, "broadcast");
PROCESS(process_unic, "runicast");
PROCESS(test_serial, "Serial line test process");
AUTOSTART_PROCESSES(&process_broad, &process_unic, &test_serial);

static int sendAddr[2];
static int rank = 1;

struct message
{
	int mType;		/* 0: Announcement: "I can be your parent"
					   1: Node down: "I lost my parent"
					   2: Data message from node to server
					   3: Open valve message, from server to node */
	int broadOrUni; /* 0: broadcast
					   1: unicast */
	int rankSender;
	int addr_dest_opening[2]; /* Used on data message and open valve message,
								 because the server has to know which one is
								 being calculated to order which one is going to open */
	int valueRead;			  /* value read and sent to the server */
};

struct routingEntry
{
	int target[2];	 // final node
	int nextJump[2]; // next node
	int alive;		 // if this connection is alive
};

static struct routingEntry routingTable[64]; // static = init to 0 for all value
static int routingTSize = sizeof(routingTable) / sizeof(routingTable[0]);

struct message rcv_msg;

static void
create_packet(int broadUni, int mesType, int rankS, int addrSend[2], int valueR)
{
	static uint8_t buf[6];
	buf[0] = broadUni;
	buf[1] = mesType;
	buf[2] = rankS;
	buf[3] = addrSend[0];
	buf[4] = addrSend[1];
	buf[5] = valueR;
	LOG_INFO("Came to create the message, rank %d", buf[2]);
	LOG_INFO_("\n");
	nullnet_buf = (uint8_t *)&buf;
	nullnet_len = sizeof(buf);
}

static void rcv_unicast(struct message rcv_msg, const linkaddr_t *src);

void input_callback(const void *data, uint16_t len,
					const linkaddr_t *src, const linkaddr_t *dest)
{
	static uint8_t bufRcv[6];

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
		{
		}
		else
		{
			if (((&rcv_msg)->mType == 2 && (&rcv_msg)->rankSender == rank + 1))
			{
				rcv_unicast(rcv_msg, src);
			}
		}
	}
}

static void rcv_unicast(struct message rcv_msg, const linkaddr_t *src)
{

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
	}
	printf("serverMessage %d %d %d \n", rcv_msg.addr_dest_opening[0], rcv_msg.addr_dest_opening[1], rcv_msg.valueRead);
}

/* Processes */

PROCESS_THREAD(process_broad, ev, data)
{
	static struct etimer timerBroad, timerRebuild;
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

	etimer_set(&timerRebuild, CLOCK_SECOND * 300);

	while (1)
	{

		etimer_set(&timerBroad, CLOCK_SECOND * 90);
		PROCESS_WAIT_EVENT();

		if (etimer_expired(&timerBroad))
		{
			LOG_INFO("Rank %d announcing...", rank);
			LOG_INFO_("\n");
			etimer_set(&timerBroad, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timerBroad));
			create_packet(0, 0, rank, addrNode, 0); // value doesn't matter here
			NETSTACK_NETWORK.output(NULL);
		}
		else if (etimer_expired(&timerRebuild))
		{
			LOG_INFO("Rebuilding the tree to optimize parents");
			LOG_INFO_("\n");

			etimer_set(&timerRebuild, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timerBroad));

			etimer_set(&timerRebuild, CLOCK_SECOND * 300);

			create_packet(0, 1, rank, addrNode, 0); // value doesn't matter here
			NETSTACK_NETWORK.output(NULL);
		}
	}

	PROCESS_END();
}

PROCESS_THREAD(process_unic, ev, data)
{
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

		PROCESS_WAIT_EVENT();
		if (!strcmp(data, "openValve"))
		{
			LOG_INFO("Will send the comamand to open");
			LOG_INFO_("\n");
			int presentInTable = 0;
			int i;

			create_packet(1, 3, rank, sendAddr, rcv_msg.valueRead); /* announce that it's down as well */

			for (i = 0; i < routingTSize; i = i + 1)
			{
				if (routingTable[i].target[0] == sendAddr[0] && routingTable[i].target[1] == sendAddr[1] && routingTable[i].alive)
				{
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
	}

	PROCESS_END();
}

PROCESS_THREAD(test_serial, ev, data)
{
	PROCESS_BEGIN();
	serial_line_init();
	uart0_set_input(serial_line_input_byte);

	while (1)
	{

		PROCESS_YIELD();
		if (ev == serial_line_event_message)
		{
			/* If we get a message from the server we copy it first */
			char *serialMsg = malloc(strlen((char *)data));

			LOG_INFO("received line: %s\n", (char *)data);

			strcpy(serialMsg, (char *)data);
			/* Then tokenize it to get the values */
			char *serverRec = strtok((char *)serialMsg, " ");

			if (strcmp(serverRec, "openValve") == 0) /* If it's according to the set standard, then it copies the values */
			{
				sendAddr[0] = atoi(strtok(NULL, " ")); /* opens the valve for the address sent */
				sendAddr[1] = atoi(strtok(NULL, " "));
				process_post(&process_unic, PROCESS_EVENT_MSG, "openValve");
			}
		}
	}
	PROCESS_END();
}
