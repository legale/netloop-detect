#!/bin/sh

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root. Restarting with sudo..."
    exec sudo "$0" "$@"
fi

# Names for the bridge and veth pairs
BRIDGE_NAME="test-br"
VETH1="veth101"
VETH2="veth111"

# Create the bridge
echo "Creating bridge $BRIDGE_NAME..."
ip link add name $BRIDGE_NAME type bridge
ip link set $BRIDGE_NAME up

# Create veth pairs
echo "Creating veth pairs: $VETH1 $VETH2"
ip link add $VETH1 type veth peer name $VETH2

# Set veth interfaces up
echo "Bringing up veth interfaces..."
ip link set $VETH1 up
ip link set $VETH2 up


# Attach veth ends to the bridge
echo "Attaching $VETH1 and $VETH2 to bridge $BRIDGE_NAME..."
ip link set $VETH1 master $BRIDGE_NAME
ip link set $VETH2 master $BRIDGE_NAME


# Monitor traffic on the bridge using tcpdump
echo "Monitoring traffic with tcpdump..."
tcpdump -i $BRIDGE_NAME -c 20 -w test-traffic.pcap > /dev/null 2>&1 &

# Start netloop-detector
echo "Starting netloop-detector..."
./netloop-detector $VETH1 3

# Cleanup
echo "Cleaning up configuration..."
ip link delete $VETH1 # Deleting one side of the veth pair will also delete its peer
ip link set $BRIDGE_NAME down
ip link delete $BRIDGE_NAME

echo "Script completed. Configuration cleaned."

