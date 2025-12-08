CC = gcc
CXX = g++
CFLAGS = -g3 -I.
CXXFLAGS = -g3 -I.

OBJ_VARIANT ?= obj
VALID_OBJ_VARIANTS := obj obj2
ifeq ($(filter $(OBJ_VARIANT),$(VALID_OBJ_VARIANTS)),)
$(error Invalid OBJ_VARIANT '$(OBJ_VARIANT)'; choose one of: $(VALID_OBJ_VARIANTS))
endif

ifeq ($(OBJ_VARIANT),obj2)
OBJ_SRC := obj2.c
else
OBJ_SRC := obj.c
endif
OBJ_OBJ := $(OBJ_VARIANT).o
VARIANT_STAMP := .variant-$(OBJ_VARIANT)

OBJS = main.o mini.l.o mini.y.o tac.o $(OBJ_OBJ) cfg.o constfold.o copyprop.o cse.o licm.o loopreduce.o loopunroll.o optlog.o deadcode.o

all: mini-optimized asm machine

mini: mini.l.c mini.y.c $(OBJS) $(VARIANT_STAMP)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

mini.l.c: mini.l
	lex -o $@ $<

mini.y.c mini.y.h: mini.y
	yacc -d -o mini.y.c mini.y

main.o: main.c mini.y.h tac.h obj.h cfg.h constfold.h copyprop.h cse.h licm.h loopreduce.h loopunroll.h optlog.h deadcode.h
	$(CC) $(CFLAGS) -c main.c -o $@

mini.l.o: mini.l.c mini.y.h tac.h
	$(CC) $(CFLAGS) -c mini.l.c -o $@

mini.y.o: mini.y.c tac.h
	$(CC) $(CFLAGS) -c mini.y.c -o $@

tac.o: tac.c tac.h
	$(CC) $(CFLAGS) -c tac.c -o $@

$(OBJ_OBJ): $(OBJ_SRC) obj.h tac.h constfold.h copyprop.h optlog.h deadcode.h
	$(CC) $(CFLAGS) -c $(OBJ_SRC) -o $@

$(VARIANT_STAMP):
	@rm -f .variant-*
	@touch $@

cfg.o: cfg.cpp cfg.h tac.h
	$(CXX) $(CXXFLAGS) -c cfg.cpp -o $@

constfold.o: constfold.cpp constfold.h optlog.h tac.h
	$(CXX) $(CXXFLAGS) -c constfold.cpp -o $@

copyprop.o: copyprop.cpp copyprop.h optlog.h tac.h
	$(CXX) $(CXXFLAGS) -c copyprop.cpp -o $@

cse.o: cse.cpp cse.h optlog.h tac.h
	$(CXX) $(CXXFLAGS) -c cse.cpp -o $@

licm.o: licm.cpp licm.h optlog.h tac.h cfg.h
	$(CXX) $(CXXFLAGS) -c licm.cpp -o $@

loopreduce.o: loopreduce.cpp loopreduce.h optlog.h tac.h
	$(CXX) $(CXXFLAGS) -c loopreduce.cpp -o $@

loopunroll.o: loopunroll.cpp loopunroll.h optlog.h tac.h
	$(CXX) $(CXXFLAGS) -c loopunroll.cpp -o $@

optlog.o: optlog.cpp optlog.h tac.h
	$(CXX) $(CXXFLAGS) -c optlog.cpp -o $@

deadcode.o: deadcode.cpp deadcode.h tac.h
	$(CXX) $(CXXFLAGS) -c deadcode.cpp -o $@

asm: asm.l asm.y inst.h
	lex -o asm.l.c asm.l
	yacc -d -o asm.y.c asm.y
	gcc -g3 asm.l.c asm.y.c -o asm

machine: machine.c inst.h
	gcc -g3 machine.c -o machine

clean:
	rm -fr *.l.* *.y.* *.s *.x *.o core mini asm machine .variant-*

test:
	./mini test.m; \
	./asm test.s; \
	./machine test.o

.PHONY: mini-optimized mini-baseline optimized baseline

mini-optimized:
	$(MAKE) OBJ_VARIANT=obj mini

mini-baseline:
	$(MAKE) OBJ_VARIANT=obj2 mini

optimized: mini-optimized

baseline: mini-baseline
