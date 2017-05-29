# You can put your build options here
-include config.mk

all: libjssp.a 

libjssp.a: jssp.o
	$(AR) rc $@ $^

%.o: %.c jssp.h
	$(CC) -c $(CFLAGS) $< -o $@

test: jssp_test
	./jssp_test

jssp_test: jssp_test.o
	$(CC) $(LDFLAGS) -L. -ljssp $< -o $@

jssp_test.o: jssp_test.c libjssp.a

simple_example: example/simple.o libjssp.a
	$(CC) $(LDFLAGS) $^ -o $@
	./simple_example

jsondump: example/jsondump.o libjssp.a
	$(CC) $(LDFLAGS) $^ -o $@


clean:
	rm -f jssp.o jssp_test.o example/simple.o
	rm -f jssp_test
	rm -f jssp_test.exe
	rm -f libjssp.a
	rm -f simple_example
	rm -f jsondump

.PHONY: all clean test

