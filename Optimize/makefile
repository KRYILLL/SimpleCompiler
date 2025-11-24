CC = gcc
CXX = g++
CFLAGS = -g3
CXXFLAGS = -g3

OBJS = main.o mini.l.o mini.y.o tac.o obj.o cfg.o constfold.o deadcode.o

all: mini asm machine

mini: mini.l.c mini.y.c $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

mini.l.c: mini.l
	lex -o $@ $<

mini.y.c mini.y.h: mini.y
	yacc -d -o mini.y.c mini.y

main.o: main.c mini.y.h tac.h obj.h cfg.h constfold.h deadcode.h
	$(CC) $(CFLAGS) -c main.c -o $@

mini.l.o: mini.l.c mini.y.h tac.h
	$(CC) $(CFLAGS) -c mini.l.c -o $@

mini.y.o: mini.y.c tac.h
	$(CC) $(CFLAGS) -c mini.y.c -o $@

tac.o: tac.c tac.h
	$(CC) $(CFLAGS) -c tac.c -o $@

obj.o: obj.c obj.h tac.h
	$(CC) $(CFLAGS) -c obj.c -o $@

cfg.o: cfg.cpp cfg.h tac.h
	$(CXX) $(CXXFLAGS) -c cfg.cpp -o $@

constfold.o: constfold.cpp constfold.h tac.h
	$(CXX) $(CXXFLAGS) -c constfold.cpp -o $@

deadcode.o: deadcode.cpp deadcode.h tac.h
	$(CXX) $(CXXFLAGS) -c deadcode.cpp -o $@

asm: asm.l asm.y inst.h
	lex -o asm.l.c asm.l
	yacc -d -o asm.y.c asm.y
	gcc -g3 asm.l.c asm.y.c -o asm

machine: machine.c inst.h
	gcc -g3 machine.c -o machine

clean:
	rm -fr *.l.* *.y.* *.s *.x *.o core mini asm machine

test:
	./mini test.m; \
	./asm test.s; \
	./machine test.o
