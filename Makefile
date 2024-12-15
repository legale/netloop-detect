# netloop detector
SRC = main.c
BIN = netloop-detector

CC     = gcc
FLAGS  += -pipe -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections -O2
DEFINE += -DLINUX -D__USE_MISC
INCLUDE = -I /usr/include/
OBJ     = $(SRC:.c=.o) 
CFLAGS  += $(FLAGS) $(INCLUDE) $(DEFINE)
LDFLAGS += -L/usr/local/lib -Wl,--gc-sections
LDLIBS = -lc

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) $(LDLIBS) -o $(BIN)

clean:
	rm -rf $(OBJ) $(BIN) *.o *.so core *.core *.pcap *~ *.pid

test:
	$(MAKE) MAKEFLAGS=-s _test
	$(MAKE) MAKEFLAGS=-s _test_post

_test_pre:
	echo Cleaning up previous configuration
	sudo ip l del veth101 >/dev/null 2>&1 || true
	sudo ip l del br-test-veth >/dev/null 2>&1 || true
	echo Setting up test environment
	sudo ip l add br-test-veth type bridge
	sudo ip l add veth101 type veth peer name veth111
	sudo ip l set veth101 master br-test-veth
	sudo ip l set veth111 master br-test-veth
	sudo ip l set veth101 up
	sudo ip l set veth111 up
	sudo ip l set br-test-veth up

_test_post:
	echo Cleaning up veth and bridge
	sudo ip l del br-test-veth >/dev/null 2>&1
	sudo ip l del veth111 >/dev/null 2>&1

_test: clean $(BIN) _test_pre
	echo tcpdump -i any -w test-traffic.pcap
	sudo tcpdump -i br-test-veth -w test-traffic.pcap > /dev/null 2>&1 & echo $$! > tcpdump.pid
	echo tcpdump started with pid: $$(cat tcpdump.pid)
	echo Running: ./$(BIN) veth101 5
	sudo ./$(BIN) veth101 5 && echo ret: $$? || echo error ret: $$?
	echo sleep 1
	sleep 1
	sudo chmod 666 test-traffic.pcap
	echo Stopping tcpdump pid: $$(cat tcpdump.pid)...
	sudo kill $$(cat tcpdump.pid) || true	
	tcpdump -r test-traffic.pcap -nn -e -vv | head -10

test_any:
	$(MAKE) MAKEFLAGS=-s _test_any
	$(MAKE) MAKEFLAGS=-s _test_post


_test_any: clean $(BIN) _test_pre
	echo tcpdump -i any -w test-traffic.pcap
	sudo tcpdump -i any -w test-traffic.pcap > /dev/null 2>&1 & echo $$! > tcpdump.pid
	echo tcpdump started with pid: $$(cat tcpdump.pid)
	echo Start test: ./$(BIN) any 8
	sudo ./$(BIN) any 5 && echo ret: $$? || echo error ret: $$?
	echo sleep 1
	sleep 1
	echo Stopping tcpdump pid: $$(cat tcpdump.pid)...
	sudo kill $$(cat tcpdump.pid) || true
	sudo chmod 666 test-traffic.pcap
	#tcpdump -r test-traffic.pcap -nn -e -vv | head -10
	rm -f 
