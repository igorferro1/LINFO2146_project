#include <stdio.h>
#include <stdlib.h>

/* Message that will be sent by the node */
struct message
{
    int mType;
    char data[32];
};

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
