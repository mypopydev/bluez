# Using bt601 with Python
import pexpect
import time
import socket
import sys
import os
import syslog

UNIX_SER="/tmp/ud_bluetooth_main"

# function to transform hex string like "0a" into signed integer
def hexStrToInt(hexstr):
    val = int(hexstr[0:2],16)
    val = (val * 6.0 - 20)/100.0
    return val

def sendMessage(message):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.connect(UNIX_SER)
    s.send(message)
    syslog.syslog("message " + str(message))
    s.close()

def bt601Conn(DEVICE):
    syslog.syslog("address: ",+ str(DEVICE))

    # Run gatttool interactively.
    syslog.syslog("Run gatttool...")
    gatt = pexpect.spawn("gatttool -I")

    # Connect to the device.
    syslog.syslog("Connecting to " + str(DEVICE))
    gatt.sendline("connect {0}".format(DEVICE))
    gatt.expect("Connection successful", timeout=5)
    syslog.syslog(" Connected!")

def bt601GetVal(DEVICE):
    syslog.syslog("address: " + str(DEVICE))

    # Run gatttool interactively.
    syslog.syslog("Run gatttool...")
    gatt = pexpect.spawn("gatttool -I")

    # Connect to the device.
    try:
        syslog.syslog("Connecting to " + str(DEVICE))
        gatt.sendline("connect {0}".format(DEVICE))
        gatt.expect("Connection successful", timeout=5)
        syslog.syslog(" Connected!")
    except pexpect.TIMEOUT:
        syslog.syslog(" Conneccting time out!")
        #sys.exit(1);
        os._exit(1)

    try:
        gatt.expect("Notification handle = 0x0012 value: 03 ", timeout=30)
        gatt.expect("\r\n", timeout=10)
        syslog.syslog("Value: ", gatt.before)
        syslog.syslog("Value 12: ", gatt.before[33:35], hexStrToInt(gatt.before[33:35]))
        sendMessage("BT601 " + str(DEVICE) + " VALUE " + str(hexStrToInt(gatt.before[33:35])))
    except pexpect.TIMEOUT:
        syslog.syslog(" Get value time out!")
        #sys.exit(1);
        os._exit(1)
    #print(float(hexStrToInt(child.before[0:5]))/100),


address = sys.argv[1]

bt601GetVal(address)
