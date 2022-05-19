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

PROCESS(process_broad, "broadcast");
PROCESS(process_unic, "runicast");
AUTOSTART_PROCESSES(&process_broad, &process_unic);

static int threshold = 5;
static int addrVal[2] = {0, 0};
static linkaddr_t parent_addr;
static int rank = 255;
static int rssi_parent = -1000;

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

/* Calculation table entry. It has the data necessary for the calculation to be done
   and to associate the data to certain node */

struct calculationEntry
{
	int node[2];
	int valueQtt;
	int isFull;
	int values[30];
	int alive;
};

struct calcTable
{
	struct calculationEntry entries[5];
	int amountOfEntries;
};

static struct calcTable calculationTable;

/**
 * Routing table entry. Each node contains a routing table that
 * tells where to forward the data
 */

struct routingEntry
{
	int target[2];	 // final node
	int nextJump[2]; // next node
	int alive;		 // if this connection is alive
};

static struct routingEntry routingTable[64];
static int routingTSize = 64;

struct message rcv_msg;

static int calculate(struct calculationEntry entry)
{

	int sx = 0;
	int sy = 0;
	int sxx = 0;
	int sxy = 0;
	int nval = 30;

	int i;
	for (i = 0; i < nval; i++)
	{
		/* x will be the indexes of the array, y are the values */
		sx = sx + i;
		sxx = sxx + (i * i);
		sy = sy + entry.values[i];
		sxy = sxy + i * entry.values[i];
	}

	return (nval * sxy - sx * sy) / (nval * sxx - (sx * sx));
}

static int isInRoutTable(int addrSensor[2], const linkaddr_t *src)
{
	int i;
	for (i = 0; i < routingTSize; i = i + 1)
	{
		if (routingTable[i].target[0] == rcv_msg.addr_dest_opening[0] && routingTable[i].target[1] == rcv_msg.addr_dest_opening[1] && routingTable[i].alive)
		{
			// if the node that sent is present on the table, we just updated from where it came from
			routingTable[i].nextJump[0] = src->u8[0];
			routingTable[i].nextJump[1] = src->u8[1];
			return 1;
		}
	}
	return 0;
}

static int isInCalcTable(int addrSensor[2], const linkaddr_t *src)
{
	int i;
	for (i = 0; i < 5; i = i + 1)
	{

		if (calculationTable.entries[i].node[0] == rcv_msg.addr_dest_opening[0] && calculationTable.entries[i].node[1] == rcv_msg.addr_dest_opening[1])
		{ /* if the node is at the table, then we add the value to the list */

			LOG_INFO("Will add to table the value %d, current is %d", rcv_msg.valueRead, calculationTable.entries[i].valueQtt);
			LOG_INFO_("\n");
			if ((calculationTable.entries[i].valueQtt % 30) == 29)
				calculationTable.entries[i].isFull = 1;

			if (!calculationTable.entries[i].isFull)
			{
				calculationTable.entries[i].values[(calculationTable.entries[i].valueQtt % 30)] = rcv_msg.valueRead;
				calculationTable.entries[i].valueQtt = calculationTable.entries[i].valueQtt + 1;
			}

			return 1;
		}
	}
	return 0;
}

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

	nullnet_buf = (uint8_t *)&buf;
	nullnet_len = sizeof(buf);
}

void rcv_broadcast(struct message rcv_msg, const linkaddr_t *src);
void rcv_unicast(struct message rcv_msg, const linkaddr_t *src);

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

void rcv_broadcast(struct message rcv_msg, const linkaddr_t *src)
{
	LOG_INFO("Rank %d received broadcast", rank);
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
				LOG_INFO("Rank %d will have a new parent", rank);
				LOG_INFO_("\n");
				addrVal[0] = src->u8[0];
				addrVal[1] = src->u8[1];
				parent_addr.u8[0] = src->u8[0];
				parent_addr.u8[1] = src->u8[1];
				rssi_parent = packetbuf_attr(PACKETBUF_ATTR_RSSI);
				rank = rcv_msg.rankSender + 1;
				process_post(&process_broad, PROCESS_EVENT_MSG, "announce");
			}
		}
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
		/* if a node lost his parent and the node is more down the tree,
			then we announce to reconnect that node */
		process_post(&process_broad, PROCESS_EVENT_MSG, "announce");
	}
}

void rcv_unicast(struct message rcv_msg, const linkaddr_t *src)
{
	/* if it's data, we have to check if the node is associated to this comp node
		if yes, we then calculate and send the command if necessary
		otherwise we just pass the data to the parent node, for it to reach other comp node
		or the server
	*/
	LOG_INFO("Comp node rank %d received unicast", rank);
	LOG_INFO_("\n");
	if (rcv_msg.mType == 2)
	{
		LOG_INFO("Comp node %d received data", rank);
		LOG_INFO_("\n");

		int presentInTable = isInRoutTable(rcv_msg.addr_dest_opening, src);
		int presentInCalcTable = isInCalcTable(rcv_msg.addr_dest_opening, src);

		/*  if it's present in the routing table but not in the calculation table, then it will check if there are
			less than 5 entries in the calculation table, and if positive, it will add it to the calculation table */

		if (presentInTable && !presentInCalcTable && calculationTable.amountOfEntries < 5)
		{
			LOG_INFO("Comp node %d received data from a node not in calc table", rank);
			LOG_INFO_("\n");
			int index = calculationTable.amountOfEntries;
			calculationTable.entries[index].node[0] = rcv_msg.addr_dest_opening[0];
			calculationTable.entries[index].node[1] = rcv_msg.addr_dest_opening[1];
			calculationTable.entries[index].alive = 1;
			calculationTable.entries[index].valueQtt = 0;
			calculationTable.entries[index].isFull = 0;
			presentInCalcTable = isInCalcTable(rcv_msg.addr_dest_opening, src); /* add value to table and update count */
			calculationTable.amountOfEntries += 1;
		}
		if (!presentInTable)
		{
			LOG_INFO("Comp node %d received data from a node not in rout table", rank);
			LOG_INFO_("\n");
			static struct routingEntry entry;
			entry.alive = 1;
			entry.target[0] = rcv_msg.addr_dest_opening[0];
			entry.target[1] = rcv_msg.addr_dest_opening[1];
			entry.nextJump[0] = src->u8[0];
			entry.nextJump[1] = src->u8[1];

			int i;
			for (i = 0; i < routingTSize; i = i + 1)
			{
				if (!(routingTable[i].alive))
				{
					routingTable[i] = entry;
					break;
				}
			}
		}

		/* If its in the table, we calculate the values instead of passing it upwards */
		int calcHere = 0;
		if (presentInCalcTable)
		{
			LOG_INFO("Comp node %d received data from a node in calc table", rank);
			LOG_INFO_("\n");
			int i;
			for (i = 0; i < 5; i = i + 1)
			{
				if (calculationTable.entries[i].node[0] == rcv_msg.addr_dest_opening[0] && calculationTable.entries[i].node[1] == rcv_msg.addr_dest_opening[1])
				{
					calcHere = 1;
					if (calculationTable.entries[i].isFull)
					{
						LOG_INFO("Comp node calculated value calculated %d", calculate(calculationTable.entries[i]));
						LOG_INFO_("\n");
						if (calculate(calculationTable.entries[i]) > threshold)
						{
							LOG_INFO("Comp node %d calculated and will send a command to open valve", rank);
							LOG_INFO_("\n");
							process_post(&process_unic, PROCESS_EVENT_MSG, "forwardCommand");
						}

						/* make a "reset" for 10 values (the time that the valve will be open) */
						calculationTable.entries[i].valueQtt = 20;
						calculationTable.entries[i].isFull = 0;
					}
				}
			}
		}

		if (!calcHere)
		{
			process_post(&process_unic, PROCESS_EVENT_MSG, "forwardData");
		}
	}
	else if (rcv_msg.mType == 3)
	{
		LOG_INFO("Received command to open");
		LOG_INFO_("\n");
		if (rcv_msg.addr_dest_opening[0] == linkaddr_node_addr.u8[0] && rcv_msg.addr_dest_opening[1] == linkaddr_node_addr.u8[1])
		{
			LOG_INFO("Error: We can't open the valve of a comp node");
			LOG_INFO_("\n");
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

/* Processes */
PROCESS_THREAD(process_broad, ev, data)
{
	static struct etimer timerBroad;
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

		etimer_set(&timerBroad, CLOCK_SECOND * 90);
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

PROCESS_THREAD(process_unic, ev, data)
{
	PROCESS_BEGIN();

	while (1)
	{

		PROCESS_WAIT_EVENT();

		if (!strcmp(data, "forwardData"))
		{
			LOG_INFO("Node rank %d will forward the data %d that received to parent %d %d", rank, rcv_msg.valueRead, (&parent_addr)->u8[0], (&parent_addr)->u8[1]);
			LOG_INFO_("\n");

			create_packet(1, 2, rank, rcv_msg.addr_dest_opening, rcv_msg.valueRead);
			linkaddr_t parentData = {{addrVal[0], addrVal[1], 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
			NETSTACK_NETWORK.output(&parentData);
		}
		else if (!strcmp(data, "forwardCommand"))
		{
			LOG_INFO("Will pass the comamand to open");
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
	}

	PROCESS_END();
}