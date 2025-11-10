all: mini asm machine

mini: main.c mini.l mini.y tac.c tac.h obj.c obj.h type.c type.h
	lex -o mini.l.c mini.l
	yacc -d -o mini.y.c mini.y
	gcc -g3 main.c mini.l.c mini.y.c tac.c obj.c type.c -o mini

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
