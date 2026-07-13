CC      = gcc
CFLAGS  = -O2 -Wall -march=znver3 -pipe
TARGET  = bogartgraph
SRCS    = main.c graph.c scan.c
OBJS    = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -m755 $(TARGET) /usr/sbin/bogartgraph

clean:
	rm -f $(OBJS) $(TARGET)
