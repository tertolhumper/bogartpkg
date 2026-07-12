CC      = gcc
CFLAGS  = -O2 -Wall -march=znver3 -pipe
LDFLAGS = -lcurl
TARGET  = check-updates
SRCS    = main.c catalog.c parse.c deps.c
OBJS    = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -m755 $(TARGET) /usr/sbin/check-updates

clean:
	rm -f $(OBJS) $(TARGET)
