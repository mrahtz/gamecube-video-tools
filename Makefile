FLAGS = `pkg-config --libs --cflags gtk+-2.0`
FLAGS += -lm

view_raw_output: view_raw_output.c
	gcc view_raw_output.c -o view_raw_output $(FLAGS)
