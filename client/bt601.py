# Using bt601 with Python
import pexpect
import time
import socket
import sys

UNIX_SER="/tmp/ud_bluetooth_main"

# function to transform hex string like "0a cd" into signed integer
def hexStrToInt(hexstr):
    val = int(hexstr[0:2],16) + (int(hexstr[3:5],16)<<8)
    if ((val&0x8000)==0x8000): # treat signed 16bits
        val = -((val^0xffff)+1)
    return val

def sendMessage(message):
    s = socket.socket(socket.AF_UNIX)
    s.connect(UNIX_SER)
    s.send(message)
    s.close()

def bt601Conn(DEVICE):
    print("address: ", DEVICE)

    # Run gatttool interactively.
    print("Run gatttool...")
    gatt = pexpect.spawn("gatttool -I")

    # Connect to the device.
    print("Connecting to ", DEVICE)
    gatt.sendline("connect {0}".format(DEVICE))
    gatt.expect("Connection successful", timeout=5)
    print(" Connected!")

def bt601GetVal(DEVICE):
    #DEVICE = "00:32:40:08:00:12"
    print("address: ", DEVICE)

    # Run gatttool interactively.
    print("Run gatttool...")
    gatt = pexpect.spawn("gatttool -I")

    # Connect to the device.
    try:
        print("Connecting to ", DEVICE)
        gatt.sendline("connect {0}".format(DEVICE))
        gatt.expect("Connection successful", timeout=5)
        print(" Connected!")
    except pexpect.TIMEOUT:
        print(" Conneccting time out!")
        sys.exit(1);

    #while True:

    # Accelerometer
    #gatt.sendline("char-write-req 0x13 0100")
    #gatt.expect("Notification handle = ", timeout=30)
    #print("Value: "),
    #print(gatt.before)
    try:
        gatt.expect("Notification handle = 0x0012 value: 03 ", timeout=30)
        gatt.expect("\r\n", timeout=10)
        print("Value: ", gatt.before)
        sendMessage(DEVICE + " DATA " + gatt.before)
    except pexpect.TIMEOUT:
        print(" Get value time out!")
        sys.exit(1);
    #print(float(hexStrToInt(child.before[0:5]))/100),


address = sys.argv[1]

bt601GetVal(address)
