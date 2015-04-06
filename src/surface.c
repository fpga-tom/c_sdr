#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <math.h>
#include "sdr.h"

static float fps;
static float load;
static float mag[FFTW_SIZE];
static float sleep_time;

static int width = 1024;
static int height = 256;
static cairo_surface_t *bitmap[256];
static unsigned char *bitmapData[256];
static int waterfallTop;

static GtkWidget *canvas;
static GtkWidget *freq_input;
static sdr_mutex_t canvas_m;

static gboolean change_freq(GtkEditable *editable, GdkEventKey *event, gpointer data) {
	int f;
	switch(event->keyval) {
		case GDK_KEY_Return:
			f = atoi(gtk_entry_get_text(GTK_ENTRY(freq_input)));
			rtl_sdr.tune(f);
			printf("freq: %u\n", f);
			break;
	}
	return FALSE;
}

static gboolean draw_points(GtkWidget *widget, cairo_t *cr, gpointer data) {
	int i;
	char fps_text[64] = {0,};
	char load_text[64] = {0,};
	char sleep_text[64] = {0,};

	sdr_mutex_lock(&canvas_m);
	sprintf(fps_text, "fps: %.2f", fps);
	sprintf(load_text, "load: %.2f", load);
	sprintf(sleep_text, "sleep: %.2f", sleep_time);


	cairo_text_extents_t te;
	cairo_set_source_rgb(cr, 0,0,0);
	cairo_select_font_face(cr, "Georgia", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 14);
//	cairo_text_extents(cr, "a", &te);
	cairo_move_to(cr, 10, 20);
	cairo_show_text(cr, fps_text);
	cairo_move_to(cr, 10, 40);
	cairo_show_text(cr, load_text);
	cairo_move_to(cr, 10, 60);
	cairo_show_text(cr, sleep_text);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, .2);
	for(i=0;i<FFTW_SIZE;i++) {
		cairo_move_to(cr,i,0);
		cairo_line_to(cr,i,300-mag[i]/10);
	}
	cairo_stroke(cr);

	for(i=0;i<256;i++) {
		cairo_set_source_surface(cr, bitmap[(i + waterfallTop)%256],0,i+300);
		cairo_paint(cr);
	}
	waterfallTop--;
	if(waterfallTop<0)
		waterfallTop+=height;
	sdr_mutex_unlock(&canvas_m);

	return TRUE;
}

// public interface

static
void draw(float* _mag, float _fps, float _load, long _sleep_time) {
	fps = _fps;
	load = _load;
	memcpy(mag, _mag, sizeof(mag));
	sleep_time = _sleep_time;
	sdr_mutex_lock(&canvas_m);
	for(int i=0;i<width;i++) {
		bitmapData[waterfallTop][i*4]=((int)mag[i]*10)&0xff;
		bitmapData[waterfallTop][i*4+1]=(((int)mag[i]*10)>>8)&0xff;
		bitmapData[waterfallTop][i*4+2]=(((int)mag[i]*10)>>16)&0xff;
	}
	gtk_widget_queue_draw(canvas);
	sdr_mutex_unlock(&canvas_m);
}

static
void open(int *argc, char ***argv) {
	int i;
	GtkBuilder *builder;
	GtkWidget *window;
	GError *er = NULL;
	gtk_init( argc, argv );

	builder = gtk_builder_new();
	if(!gtk_builder_add_from_file(builder, "/home/tomas/c_sdr/gui/app.glade", &er)) {
		sdr_log(ERROR,er->message);
	}
	window = GTK_WIDGET( gtk_builder_get_object( builder, "applicationwindow1" ) );
	canvas = GTK_WIDGET( gtk_builder_get_object( builder, "drawingarea1" ) );
	freq_input = GTK_WIDGET( gtk_builder_get_object( builder, "entry1" ) );

	g_signal_connect(G_OBJECT(canvas), "draw", G_CALLBACK(draw_points), NULL); 
	g_signal_connect(G_OBJECT(freq_input), "key-press-event", G_CALLBACK(change_freq), NULL); 

	/* Destroy builder */
    g_object_unref( G_OBJECT( builder ) );

	sdr_mutex_init(&canvas_m);
	cairo_format_t format = CAIRO_FORMAT_RGB24;
	int stride = cairo_format_stride_for_width (format, width);
	printf("stride %d\n", stride);
	for(i=0;i<height;i++) {
		bitmapData[i] = (unsigned char *)(malloc (stride));
		bitmap[i] = cairo_image_surface_create_for_data (
				bitmapData[i], format, width, 1, stride);
	}

    /* Show main window and start main loop */
    gtk_widget_show( window );
}

static
void start() {
	gtk_main();
}

surface_t surface = {
	.open = open,
	.start = start,
	.draw = draw
};
