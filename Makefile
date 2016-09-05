CFLAGS=
LDFLAGS=`pkg-config --static --libs glfw3` -lvulkan

.PHONY: test clean

default: main

main:
	g++ $(CFLAGS) -o test src/main.cpp $(LDFLAGS)

shaders:
	glslangValidator -V shaders/shader.vert -o vert.spv
	glslangValidator -V shaders/shader.frag -o frag.spv

clean:
	rm test vert.spv frag.spv
