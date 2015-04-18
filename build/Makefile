OBJECTS:=sdr.o\
	scheduler.o\
	analyzer.o\
	surface.o\
	dsp.o\
	main.o\
	wb.o
all: $(OBJECTS)
	clang -O3 -o sdr $(OBJECTS) -lm -lc -lliquid -lpthread -lfftw3 -lpulse-simple -lpulse `pkg-config --cflags --libs gtk+-3.0`

gr_rds_data_decoder.o:../src/gr_rds_data_decoder.cc
	clang -c $<
%.o:../src/%.c
	clang -O3 -c $< `pkg-config --cflags gtk+-3.0`

clean:
	rm *.o
