from optparse import Values
import socket    
import socket
import argparse
import sys

PORT = 60001        
THRESHOLD = 5
VAL_NUM = 30
values = []
# values will store the values, we'll have an array with:
#[ [[address], current index, [values]] ]
        
def leastSquareSlope(values):
    # formula from https://www.cuemath.com/data/least-squares/

    sx = sy = sxx = sxy = 0

    for i in range(VAL_NUM):
        sx += i
        sxx += (i**2)
        sy += values[i]
        sxy += (i * values[i])

    return (VAL_NUM * sxy - (sx * sy)) /( VAL_NUM * sxx - (sx ** 2))

def recv(sock):
    data = sock.recv(1)
    buf = b""
    while data.decode("utf-8") != "\n":
        buf += data
        data = sock.recv(1)
    return buf.decode("utf-8")

def valuesAdd(address, value):
    for i in values:
        if (i[0][0]==address[0] and i[0][1]==address[1]):
            #print("size of the value table = " + str(len(i[2])))

            # i[1] = (i[1] + 1) % VAL_NUM # Calculate the new quantity of values
            
            # if (len(i[2]) == VAL_NUM and i[1] == 30):
            #     # i[2][i[3]] = value
            #     i[1] = 20 
            #     return least_square(i[2], (i[3]+1) % VAL_NUM)
            
            # else:
            i[2].append(value) # Adding value to the list
            i[1] = (i[1] + 1) % VAL_NUM  # Updating the current index

            if(len(i[2]) == VAL_NUM):    # If it's full, then calculate
                calculation = leastSquareSlope(i[2])
                print(calculation)
                i[1] = 20
                for j in range(0, 10):
                    del i[2][j]
                print("Deleted 10 elements")
                print(i[2])
                print(len(i[2]))
                if calculation > THRESHOLD:
                    return 'OPEN'

            return 0

    values.append([address, 0, [value]])
    return 0

def main(ip, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ip, port))

    while(True):
            
            received = recv(sock)             
                # When it's finished (when it's a "\n"), then it will treat the received data
            readData = received.split() # First we separate the values by the blankspace
            if readData[0] == "serverMessage":   # The message is like "serverMessage address[0] address[1] readValue"
                
                address = [int(readData[1]), int(readData[2])]
                print("Received sensor data from " + str(address))
                command = valuesAdd(address, float(readData[3]))
                
                if command == 'OPEN' :
                    print("opening valves of node address" + str(address))
                    sock.send(("openValve "+ str(address[0]) + " " + str(address[1]) + " \n").encode("utf-8"))
                

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", dest="ip", type=str)
    parser.add_argument("--port", dest="port", type=int)
    args = parser.parse_args()

    main(args.ip, args.port)