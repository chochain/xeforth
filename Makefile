CC = g++
CC_FLAG = -std=c++17 -g -O3 -Wall -pthread \
          -fomit-frame-pointer -fno-stack-check -fno-stack-protector \
		  -march=native -ffast-math -funroll-loops

SRCS = \
	src/ceforth.cpp 	 \
	src/ceforth_sys.cpp  \
	src/ceforth_task.cpp \

ESPS = \
	src/esp32/xserver.cpp \
	src/esp32/xforth.cpp  \
	src/esp32/xgl.cpp

FLST = \
	tests/eforth \
	tests/xeforth

exe: tests/eforth

esp: tests/xeforth

all: exe

%.o: %.cpp
	$(CC) $(CC_FLAG) -Isrc -c -o $@ $<

tests/eforth: platform/main.o $(SRCS:%.cpp=%.o)
	$(CC) $(CC_FLAG) -o $@ $^

tests/xeforth: platform/main.o $(SRCS:%.cpp=%.o) $(ESPS:%.cpp=%.o)
	$(CC) $(CC_FLAG) -o $@ $^

debug: tests/eforth
	/bin/valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes $^

clean:
	rm platform/main.o src/*.o src/esp32/*.o $(FLST)


