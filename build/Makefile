all:
	clang -O3 -o sdr ../src/sdr.c ../src/scheduler.c ../src/analyzer.c ../src/surface.c ../src/dsp.c ../src/main.c -lm -lc -lliquid -lpthread -lfftw3 -lpulse-simple -lpulse `pkg-config --cflags --libs gtk+-3.0`
