build:
	gcc main.c -o main `pkg-config --cflags --libs gstreamer-1.0`
build-debug:
	gcc main.c -O0 -g -o main `pkg-config --cflags --libs gstreamer-1.0`