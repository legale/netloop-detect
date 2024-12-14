# netloop-detector
Simple network loop detector based on idea here: https://blogs.cisco.com/networking/preventing-network-loops-a-feature-you-need-to-be-aware-of

## compilation
Tested on debian12 linux.
```sh
git clone <url> netloop-detector
cd netloop-detector
make
```

## usage
### specified interface
```sh
./netloop-detector <iface> <timeout_sec>
```

### every interface in the system
```sh
./netloop-detector any 5
```

### local test

- A veth pair and a bridge interface will be created.
- Both ends of the veth pair will be added to the created bridge.
- tcpdump will be started on the bridge to capture traffic.
- netloop-detector will run and attempt to detect loops.
- The veth pair and the bridge will be removed after the test.
- The first 10 entries in the resulting PCAP file will be displayed.

```sh
make test
```

