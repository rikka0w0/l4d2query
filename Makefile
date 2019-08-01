CC := cc
CFLAGS := -g

main: l4d2query main.c
	$(CC) $(CFLAGS) -o l4d2query l4d2query.o main.c

l4d2query: l4d2query.c l4d2query.h
	$(CC) -c $(CFLAGS) l4d2query.c

clean:
	rm -f l4d2query l4d2query.o
