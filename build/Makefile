OBJECTS:=sdr.o\
	scheduler.o\
	analyzer.o\
	surface.o\
	dsp.o\
	main.o\
	wb.o
CFLAGS:=-MMD -O3
all: $(OBJECTS)
	clang -O3 -o sdr $(OBJECTS) -lm -lc -lliquid -lpthread -lfftw3 -lpulse-simple -lpulse `pkg-config --cflags --libs gtk+-3.0`

gr_rds_data_decoder.o:../src/gr_rds_data_decoder.cc
	clang $(CFLAGS) -c $<
%.o:../src/%.c
	clang $(CFLAGS) -c $< `pkg-config --cflags gtk+-3.0`
-include $(OBJECTS:%.o=%.d)

clean:
	rm *.o
