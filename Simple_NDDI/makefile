CC = gcc
CFLAGS = -T linker.ld -no-pie
LIBS = -lm

quadratic_solver: main.o calc_d.o solve_qe.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o quadratic_solver
