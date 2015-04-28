CC=g++
CCFLAGS=-std=c++11
OBJFILES= alu.o cpu.o except.o instrset.o mem.o periph.o regbnk.o main.o periph/mac.o stats/stats.o

all: sim

sim: $(OBJFILES)
	$(CC) -o $@ $^ $(CCFLAGS)

%.o: %.cpp
	$(CC) -c $< -o $@ $(CCFLAGS)

clean:
	rm -f *.o
