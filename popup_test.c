/*
 * popup_test.c -- simple program to demonstrate issue with xdg-popup configure
 * 	event in Wayfire (based on wlroots examples)
 * 
 * 2021 Daniel Kondor <kondor.dani@gmail.com>
 */

#define WLR_USE_UNSTABLE
#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <wayland-client.h>
#include <wayland-server.h>
#include "xdg-shell-client-protocol.h"

#include <cairo.h>


#define UNUSED __attribute__((unused))

static int width = 400, height = 300;
static int popup_width = 300, popup_height = 300;

static struct wl_display* display = NULL;
static struct wl_compositor* compositor = NULL;
static struct wl_seat* seat = NULL;
static struct wl_shm* wl_shm = NULL;
static struct xdg_wm_base* wm_base = NULL;

static struct wl_surface* surface = NULL;
static struct xdg_toplevel* toplevel = NULL;
static struct xdg_surface *xdg_surface = NULL;
static struct wl_surface* popup_surface = NULL;
static struct xdg_popup* popup = NULL;
static struct xdg_surface* popup_xdg_surface = NULL;

int pointer_x = 0;
int pointer_y = 0;

static const char app_id[] = "xdg-popup-test";


static int shm_create(size_t size) {
    int shm_fd = memfd_create("popup_test", MFD_CLOEXEC);
    if(shm_fd < 0) return -1;
    int ret;
    errno = 0;
    do { ret = ftruncate(shm_fd, size); } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(shm_fd);
        return -1;
    }
    return shm_fd;
}

static void wl_buffer_release(UNUSED void *data, struct wl_buffer *wl_buffer) {
	wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener buffer_listener = {
	.release = wl_buffer_release,
};

static void render_frame(struct wl_surface* wls) {
	int w = (wls == popup_surface) ? popup_width : width;
	int h = (wls == popup_surface) ? popup_height : height;
	if(!w) w = 400;
	if(!h) h = 300;
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
	size_t size = stride * h * 4;
	int shm_fd = shm_create(size);
	if(shm_fd < 0) goto render_frame_error;
	unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(data == MAP_FAILED) {
		close(shm_fd);
		goto render_frame_error;
    }
	
	struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm, shm_fd, size);
	struct wl_buffer* buffer = NULL;
	if(pool) {
		buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
		wl_shm_pool_destroy(pool);
	}
	close(shm_fd);
	if(!buffer) {
		munmap(data, size);
		goto render_frame_error;
	}
	
	cairo_surface_t* csurface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
	cairo_t* cr = cairo_create(csurface);
	cairo_set_source_rgba(cr, 0, 0, wls == popup_surface ? 0.7 : 0, 1);
	cairo_paint(cr);
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 14);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, 50, 150);
	cairo_show_text(cr, (wls == popup_surface ? "Example popup" :
		"Example toplevel (click for popup)"));
	cairo_surface_flush(csurface);
	
	wl_buffer_add_listener(buffer, &buffer_listener, NULL);
	
	wl_surface_attach(wls, buffer, 0, 0);
	wl_surface_commit(wls);
	
	cairo_destroy(cr);
    cairo_surface_destroy(csurface);
	
	munmap(data, size);
	
    return;
	
render_frame_error:
	fprintf(stderr, "Cannot create shared memory for rendering!\n");
}

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);
	
	render_frame((struct wl_surface*)data);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(UNUSED void *data,
		UNUSED struct xdg_toplevel *xdg_toplevel, int32_t w, int32_t h,
		UNUSED struct wl_array *states) {
	width = w;
	height = h;
}

static void xdg_toplevel_handle_close(UNUSED void *data,
		UNUSED struct xdg_toplevel *xdg_toplevel) {
	exit(0);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

static void xdg_popup_handle_configure(UNUSED void* data,
		UNUSED struct xdg_popup *xdg_toplevel, int32_t x,
		int32_t y, int32_t w, int32_t h) {
	fprintf(stderr, "\nPopup configure: (%d, %d, %d, %d)\n", x, y, w, h);
	popup_width = w;
	popup_height = h;
}

static void xdg_popup_handle_done(UNUSED void* data, struct xdg_popup* popup1) {
	xdg_popup_destroy(popup1);
	popup = NULL;
}

static const struct xdg_popup_listener xdg_popup_listener = {
	.configure = xdg_popup_handle_configure,
	.popup_done = xdg_popup_handle_done
};


static void xdg_wm_base_ping(UNUSED void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};


static void create_popup() {
	if(popup) { xdg_popup_destroy(popup); popup = NULL; }
	if(popup_xdg_surface) { xdg_surface_destroy(popup_xdg_surface); popup_xdg_surface = NULL; }
	if(popup_surface) { wl_surface_destroy(popup_surface); popup_surface = NULL; }
	
	struct xdg_positioner* positioner = xdg_wm_base_create_positioner(wm_base);
	assert(positioner);
	xdg_positioner_set_size(positioner, popup_width, popup_height);
	xdg_positioner_set_anchor_rect(positioner, pointer_x, pointer_y, 1, 1);
	xdg_positioner_set_anchor(positioner, 5);
	xdg_positioner_set_gravity(positioner, 2);
	xdg_positioner_set_constraint_adjustment(positioner, 3);
	
	popup_surface = wl_compositor_create_surface(compositor);
	assert(popup_surface);
	popup_xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, popup_surface);
	assert(popup_xdg_surface);
	popup = xdg_surface_get_popup(popup_xdg_surface, xdg_surface, positioner);
	assert(popup);
	
	xdg_surface_add_listener(popup_xdg_surface, &xdg_surface_listener, popup_surface);
	xdg_popup_add_listener(popup, &xdg_popup_listener, popup_surface);
	
	wl_surface_commit(popup_surface);
	xdg_positioner_destroy(positioner);
	wl_display_roundtrip(display);
}

static int pointer_popup = 0;

static void pointer_handle_button(UNUSED void *data, UNUSED struct wl_pointer *pointer, UNUSED uint32_t serial,
		UNUSED uint32_t time, UNUSED uint32_t button, uint32_t state_w) {
	if(!state_w) return;
	if(pointer_popup && popup) {
		xdg_popup_destroy(popup);
		popup = NULL;
	}
	else {
		create_popup();
	}
}

static void pointer_handle_enter(UNUSED void *data, UNUSED struct wl_pointer *wl_pointer,
		UNUSED uint32_t serial, struct wl_surface *surface,
		UNUSED wl_fixed_t surface_x,UNUSED  wl_fixed_t surface_y) {
	if(surface == popup_surface) pointer_popup = 1;
	else {
		pointer_popup = 0;
		pointer_x = wl_fixed_to_int(surface_x);
		pointer_y = wl_fixed_to_int(surface_y);
	}
}

static void pointer_handle_leave(UNUSED void *data, UNUSED struct wl_pointer *wl_pointer,
		UNUSED uint32_t serial, struct wl_surface *surface) {
	if(surface == popup_surface) pointer_popup = 0;
}

static void pointer_handle_motion(UNUSED void *data, UNUSED struct wl_pointer *wl_pointer,
		UNUSED uint32_t time, UNUSED wl_fixed_t surface_x, UNUSED wl_fixed_t surface_y) {
	pointer_x = wl_fixed_to_int(surface_x);
	pointer_y = wl_fixed_to_int(surface_y);
}

static void pointer_handle_axis(UNUSED void *data, UNUSED struct wl_pointer *wl_pointer,
		UNUSED uint32_t time, UNUSED uint32_t axis, UNUSED wl_fixed_t value) {
	// This space intentionally left blank
}

static void pointer_handle_frame(UNUSED void *data, UNUSED struct wl_pointer *wl_pointer) {
	// This space intentionally left blank
}

static void pointer_handle_axis_source(UNUSED void *data,
		UNUSED struct wl_pointer *wl_pointer, UNUSED uint32_t axis_source) {
	// This space intentionally left blank
}

static void pointer_handle_axis_stop(UNUSED void *data,
		UNUSED struct wl_pointer *wl_pointer, UNUSED uint32_t time, UNUSED uint32_t axis) {
	// This space intentionally left blank
}

static void pointer_handle_axis_discrete(UNUSED void *data,
		UNUSED struct wl_pointer *wl_pointer, UNUSED uint32_t axis, UNUSED int32_t discrete) {
	// This space intentionally left blank
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
	.frame = pointer_handle_frame,
	.axis_source = pointer_handle_axis_source,
	.axis_stop = pointer_handle_axis_stop,
	.axis_discrete = pointer_handle_axis_discrete,
};



static void handle_global(UNUSED void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if(strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 1);
	} else if(strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wm_base, &xdg_wm_base_listener, NULL);
	} else if(strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
	} else if(strcmp(interface, wl_shm_interface.name) == 0) {
        wl_shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	}
}

static void handle_global_remove(UNUSED void *data,UNUSED  struct wl_registry *registry,UNUSED  uint32_t name) {
	// who cares
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

int main(int argc, char **argv)
{
	display = wl_display_connect(NULL);
	assert(display);
	struct wl_registry *registry = wl_display_get_registry(display);
	assert(registry);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	assert(compositor && seat && wm_base && wl_shm);
	
	struct wl_pointer *pointer = wl_seat_get_pointer(seat);
	wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	
	surface = wl_compositor_create_surface(compositor);
	assert(surface);
	xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	assert(xdg_surface);
	struct xdg_toplevel *xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
	assert(xdg_toplevel);
	toplevel = xdg_toplevel;
	
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, surface);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, surface);
	
	xdg_toplevel_set_title(xdg_toplevel, app_id);
	xdg_toplevel_set_app_id(xdg_toplevel, app_id);
	
	wl_surface_commit(surface);
	
	wl_display_roundtrip(display);
	// render_frame();
	
	while (wl_display_dispatch(display) != -1);
	
	return 0;
}

