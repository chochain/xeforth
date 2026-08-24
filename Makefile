CC = g++
CC_FLAG = -std=c++17 -g -O3 -Wall -pthread \
          -fomit-frame-pointer -fno-stack-check -fno-stack-protector \
		  -march=native -ffast-math -funroll-loops

FLST = \
	tests/eforth

exe: tests/eforth

all: exe

%.o: %.cpp
	$(CC) $(CC_FLAG) -Isrc -c -o $@ $<

tests/eforth: platform/main.o src/ceforth.o src/ceforth_sys.o src/ceforth_task.o
	$(CC) $(CC_FLAG) -o $@ $^

debug: tests/eforth
	/bin/valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes $^

clean:
	rm src/*.o platform/*.o $(FLST)


