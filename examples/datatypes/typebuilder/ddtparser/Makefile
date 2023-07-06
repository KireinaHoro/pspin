MPICXX=mpic++
CXX=mpic++
LEX=flex

.PHONY: all test clean libddtparser.a 

all: parser libddtparser.a 

FARCDIR=..

LDLIBS=$(shell llvm-config-6.0 --libs all)
LDFLAGS=$(shell llvm-config-6.0 --ldflags) #-dynamic
CPPFLAGS=-DHRT_ARCH=2 -O3 -I/usr/include/mpi -I$(FARCDIR) -I./hrtimer

OBJECTS= parser.o lexer.o

lexer.o: 
	$(CC) -c -o lexer.o lexer.c

parser.o: 
	$(CXX) -c -o parser.o parser.cpp  $(CPPFLAGS)


libddtparser.a: parser.o lexer.o 
	ar rcs $@ $^
	ranlib $@


#	$(MPICXX) -o $@ $^ $(LDFLAGS) $(LDLIBS) 


parser: parser.y lexer.lex
	bison -d -o parser.cpp parser.y
	flex -o lexer.c lexer.lex

clean:
	rm -f ddtplayer *.o
	rm -f lexer.c parser.cpp libddtparser.a

