view_raw_output:
	gcc view_raw_output.c -o view_raw_output $(pkg-config --libs --cflags gtk+-2.0) -lm 
