CFLAGS=
LDFLAGS=`pkg-config --static --libs glfw3` -lvulkan

.PHONY: test clean

default: main

main:
	g++ $(CFLAGS) -o test src/main.cpp $(LDFLAGS)

clean:
	rm test
