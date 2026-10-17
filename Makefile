CC      = gcc
CFLAGS  = -O2 -Wall -march=znver3 -pipe
LDFLAGS = -lcurl -larchive
TARGET  = check-updates
SRCS    = main.c catalog.c parse.c deps.c archdb.c
HDRS    = catalog.h parse.h deps.h archdb.h
OBJS    = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -m755 $(TARGET) /usr/sbin/check-updates

clean:
	rm -f $(OBJS) $(TARGET)
