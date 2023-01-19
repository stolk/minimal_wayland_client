//
// (Almost) minimal wayland client.
// Client code that creates a EGL surface, and draws in it.
// In addition it uses the dmabuf protocol to list the supported pixel formats.
//
// (c)2023 by Bram Stolk (b.stolk@gmail.com)
//

#include <sys/ioctl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
//#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <wayland-client-core.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>

#include <linux/videodev2.h>

#include "xdg-shell-client-protocol.h" // Include code generated with wayland-scanner.

#include "linux-dma-protocol.h"

#define MAXBUF	4

// OpenGLES

static EGLNativeDisplayType	native_dpy;
static EGLNativeWindowType	native_win;
static EGLDisplay*		egl_dpy;
static EGLContext		egl_ctx;
static EGLSurface		egl_srf;

// video
static int			vid_fd;
static uint32_t			vid_fourcc;
static enum v4l2_buf_type	vid_buffer_type;
static int			vid_num_planes;
static uint32_t			vid_resolution[2];
static uint32_t			vid_strides[VIDEO_MAX_PLANES];
static struct v4l2_buffer	vid_buffers[MAXBUF];
static struct v4l2_plane	vid_planes[MAXBUF][VIDEO_MAX_PLANES];
static const int		vid_num_buffers = MAXBUF;
static int			vid_dma_fds[MAXBUF][VIDEO_MAX_PLANES];

// Wayland

static struct wl_compositor*	compositor;
static struct wl_surface*	surface;
static struct wl_region*	region;

static struct xdg_wm_base*	wm_base;
static struct xdg_surface*	xdg_surface;
static struct xdg_toplevel*	xdg_toplevel;

static struct zwp_linux_dmabuf_v1* dmabuf;

// Application

static int32_t			winw = 1280;
static int32_t			winh =  720;
static int			done =    0;


// xdg toplevel handling

static void xdg_toplevel_handle_configure
(
	void *data,
	struct xdg_toplevel *toplvl,
	int32_t w,
	int32_t h,
	struct wl_array *states
)
{
	(void) data;
	(void) toplvl;
	(void) states;
	if(w == 0 && h == 0)
		return;
	if(winw != w || winh != h)
	{
		winw = w;
		winh = h;
		wl_egl_window_resize(native_win, winw, winh, 0, 0);
		wl_surface_commit(surface);
	}
}

static void xdg_toplevel_handle_close
(
	void *data,
	struct xdg_toplevel *xdg_toplevel
)
{
	(void) data;
	(void) xdg_toplevel;
	done = 1;
}

static struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};


// xdg surface handling

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	(void) data;
	xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener =
{
	.configure = xdg_surface_configure,
};


// xdg wm base handling

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
	(void) data;
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener =
{
	.ping = xdg_wm_base_ping,
};


// dma buf protocol

static void params_created(void* data, struct zwp_linux_buffer_params_v1* params, struct wl_buffer* new_buffer)
{
	(void)data;
	(void)params;
	(void)new_buffer;
	fprintf(stderr,"dmabuf params created\n");
}


static void params_failed(void* data, struct zwp_linux_buffer_params_v1* params)
{
	(void)data;
	(void)params;
	fprintf(stderr,"dmabuf params creation failed.\n");
}


static const struct zwp_linux_buffer_params_v1_listener params_create_listener =
{
	.created = params_created,
	.failed  = params_failed,
};

static void dmabuf_format(void* data, struct zwp_linux_dmabuf_v1* zwp_linux_dmabuf, uint32_t format)
{
	// DEPRECATED, but still required to be part of protocol: On mutter, it will abort without it.
	(void)data;
	(void)zwp_linux_dmabuf;
	(void)format;
}


static void dmabuf_modifier(void* data, struct zwp_linux_dmabuf_v1* zwp_linux_dmabuf, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo)
{
	(void)data;
	(void)zwp_linux_dmabuf;
	const uint64_t modifier = modifier_lo | ((uint64_t)modifier_hi) << 32UL;
	if (modifier == 0)
		fprintf
		(
			stderr,
			"dmabuf listener found format %c%c%c%c\n",
			(format>>0)&0xff, (format>>8)&0xff, (format>>16)&0xff, (format>>24)&0xff
		);
}


static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
	.format = dmabuf_format,
	.modifier = dmabuf_modifier,
};


// registry handling

static void global_registry_handler
(
	void *data,
	struct wl_registry *registry,
	uint32_t id,
	const char *interface,
	uint32_t version
)
{
	(void) data;
	(void) id;
	(void) interface;
	(void) version;
	//fprintf(stderr, "Registry event for %s id %d\n", interface, id);
	if (strcmp(interface, "wl_compositor") == 0) {
		compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wm_base, &xdg_wm_base_listener, NULL);
	} else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
		dmabuf = wl_registry_bind(registry, id, &zwp_linux_dmabuf_v1_interface, 3);
		zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, 0);
	}
}


static void global_registry_remover
(
	void *data,
	struct wl_registry *registry,
	uint32_t id
)
{
	(void) data;
	(void) registry;
	fprintf(stderr, "registry_remover for %d\n", id);
}

static const struct wl_registry_listener registry_listener =
{
	global_registry_handler,
	global_registry_remover
};


// video code

static int xioctl(int fd, unsigned long request, void *arg)
{
	int r;
	do {
		r = ioctl(fd, request, arg);
	} while (r == -1 && errno == EINTR);
        return r;
}


int setup_video(const char* devname, const uint32_t required_format, const int plane_count)
{
	// Open the video device.
	vid_fourcc = required_format;
	vid_fd = open(devname, O_RDWR);
	if (vid_fd < 0)
		return -1;
	struct v4l2_capability cap;
	if (xioctl(vid_fd, VIDIOC_QUERYCAP, &cap) == -1)
	{
		fprintf(stderr, "QUERYCAP failed: %s\n", strerror(errno));
		close(vid_fd);
		return -1;
	}
	// Make sure it can capture multiplanar video using dma.
	const int multiplanar_capable =
		cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE;
	const int streaming_capable =
		cap.capabilities & V4L2_CAP_STREAMING;

	if (!streaming_capable)
	{
		fprintf(stderr, "Video device %s cannot stream.\n", devname);
		close(vid_fd);
		return -1;
	}

	fprintf(stderr, "Video device %s multi-plane capable: %s\n", devname, multiplanar_capable ? "YES" : "NO");
	if (!multiplanar_capable && plane_count > 1)
	{
		close(vid_fd);
		return -1;
	}

	vid_buffer_type = plane_count > 1 ? V4L2_CAP_VIDEO_CAPTURE_MPLANE : V4L2_CAP_VIDEO_CAPTURE;

	// It's not clear to me why DMABUF access does not work here.
	// Why do we have to do MMAP first?
	const enum v4l2_memory memory_access_type = V4L2_MEMORY_MMAP;
	//const enum v4l2_memory memory_access_type = V4L2_MEMORY_DMABUF;

	// See what current format is.
	struct v4l2_format current_format;
	memset(&current_format, 0, sizeof(current_format));
	current_format.type = vid_buffer_type;
	if (xioctl(vid_fd, VIDIOC_G_FMT, &current_format) < 0)
	{
		fprintf(stderr, "VIDIOC_G_FMT failed: %s\n", strerror(errno));
		close(vid_fd);
		return -1;
	}
	vid_resolution[0] = current_format.fmt.pix.width;
	vid_resolution[1] = current_format.fmt.pix.height;
	vid_strides[0] = current_format.fmt.pix.bytesperline;
	const uint32_t fourcc = current_format.fmt.pix.pixelformat;
	fprintf
	(
		stderr,
		"Current setting has type 0x%x and pixelformat %c%c%c%c\n",
		current_format.type,
		(fourcc>> 0) & 0xff,
		(fourcc>> 8) & 0xff,
		(fourcc>>16) & 0xff,
		(fourcc>>24) & 0xff
	);
	fprintf
	(
		stderr,
		"Current resolution: %dx%d with stride %d\n",
		vid_resolution[0], vid_resolution[1], vid_strides[0]
	);

	vid_num_planes = plane_count;
	struct v4l2_requestbuffers request;
	memset(&request, 0, sizeof(request));
	request.type = vid_buffer_type;
	request.memory = memory_access_type;
	request.count = vid_num_buffers * vid_num_planes;
	if (xioctl(vid_fd, VIDIOC_REQBUFS, &request) < 0)
	{
		fprintf(stderr, "VIDEOC_REQBUFS failed: %s\n", strerror(errno));
		close(vid_fd);
		return -1;
	}
	if (request.count < (uint32_t)vid_num_buffers * vid_num_planes)
	{
		fprintf(stderr, "VIDEOC_REQBUFS fell short with only %d buffers.\n", request.count);
		close(vid_fd);
		return -1;
	}
	fprintf(stderr, "Created %d buffers\n", request.count);

	// Queue and export
	for (int b=0; b<vid_num_buffers; ++b)
	{
		// Queue
		struct v4l2_buffer* buf = vid_buffers + b;
		memset(buf, 0, sizeof(struct v4l2_buffer));
		buf->type = vid_buffer_type;
		buf->memory = memory_access_type;
		buf->index = b;
		if (xioctl(vid_fd, VIDIOC_QUERYBUF, buf) < 0)
		{
			fprintf(stderr, "VIDIOC_QUERYBUF failed: %s\n", strerror(errno));
			close(vid_fd);
			return 0;
		}
		fprintf(stderr, "buffer %d has type 0x%x size %u and offset %08x\n", b, buf->type, buf->length, buf->m.offset);

		if (vid_num_planes > 1)
		{
			fprintf(stderr, "vid_num_planes: %d\n", vid_num_planes);
			buf->length = VIDEO_MAX_PLANES;
			buf->m.planes = vid_planes[b];
		}
		if (xioctl(vid_fd, VIDIOC_QBUF, buf) < 0)
		{
			fprintf(stderr, "VIDIOC_QBUF failed for buffer %d: %s\n", b, strerror(errno));
			close(vid_fd);
			return -1;
		}

		// Export the dma buffers of the video device.
		struct v4l2_exportbuffer exp;
		memset(&exp, 0, sizeof(exp));
		for (int p=0; p<vid_num_planes; ++p)
		{
			exp.type = vid_buffer_type;
			exp.index = b;
			exp.plane = p;
			if (xioctl(vid_fd, VIDIOC_EXPBUF, &exp) < 0)
			{
				fprintf
				(
					stderr,
					"VIDIOC_EXPBUF failed for buffer %d, plane %d: %s\n",
					b, p, strerror(errno)
				);
				close(vid_fd);
				return -1;
			}
			vid_dma_fds[b][p] = exp.fd;
			fprintf(stderr, "Buffer %d plane %d uses fd %d\n", b, p, vid_dma_fds[b][p]);
		}
	}
	fprintf(stderr, "Exported %d dma buffers from video device.\n", vid_num_buffers * vid_num_planes);
	return 0;
}

// dmabuf code

void create_dma_buffer(int buf_nr, int plane_nr)
{
	const int fd = vid_dma_fds[buf_nr][plane_nr];
	struct zwp_linux_buffer_params_v1* params = 0;
	uint64_t modifier = 0;
	params = zwp_linux_dmabuf_v1_create_params(dmabuf);
	for (int i=0; i<vid_num_planes; ++i)
	{
		int stride = vid_strides[0];
		int offset = 0;
		fprintf(stderr, "adding parameter fd=%d plane=%d offset=%d stride=%d\n", fd, i, offset, stride);
		zwp_linux_buffer_params_v1_add
		(
			params,
			fd,
			i,
			offset,
			stride,
			modifier >> 32,
			modifier & 0xffffffff
		);
	}
	if (zwp_linux_buffer_params_v1_add_listener(params, &params_create_listener, 0) < 0)
		fprintf(stderr, "Failed to add linux buffer params listener.\n");
	uint32_t flags = 0;
	zwp_linux_buffer_params_v1_create
	(
		params,
		vid_resolution[0],
		vid_resolution[1],
		vid_fourcc,
		flags
	);
}


static void create_dma_buffers(void)
{
	for (int b=0; b<vid_num_buffers; ++b)
		for (int p=0; p<vid_num_planes; ++p)
			create_dma_buffer(b, p);
}

// OpenGL ES code

static EGLBoolean CreateEGLContext ()
{
	egl_dpy = eglGetDisplay(native_dpy);
	if ( egl_dpy == EGL_NO_DISPLAY )
	{
		fprintf(stderr, "eglGetDisplay() returned EGL_NO_DISPLAY.\n");
		return EGL_FALSE;
	}

	// Initialize EGL
	EGLint majorVersion=0;
	EGLint minorVersion=0;
	if ( !eglInitialize(egl_dpy, &majorVersion, &minorVersion) )
	{
		fprintf(stderr,"eglInitialize() returned EGL_FALSE.\n");
		return EGL_FALSE;
	}

	// Get configs
	EGLint numConfigs=0;
	const EGLBoolean got_configs = eglGetConfigs(egl_dpy, NULL, 0, &numConfigs);
	if (!got_configs)
	{
		fprintf(stderr, "eglGetConfigs() returned EGL_FALSE.\n");
		return EGL_FALSE;
	}
	if (numConfigs == 0)
	{
		fprintf(stderr, "eglGetConfigs() yielded 0 configs.\n");
		return EGL_FALSE;
	}

	// Choose config
	EGLint fbAttribs[] =
	{
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE,        8,
		EGL_GREEN_SIZE,      8,
		EGL_BLUE_SIZE,       8,
#if 0
		EGL_SAMPLE_BUFFERS,  1,
		EGL_SAMPLES,         4, // 4x MSAA
#endif
		EGL_NONE
	};
	EGLConfig config = 0;
	const int chosen = eglChooseConfig(egl_dpy, fbAttribs, &config, 1, &numConfigs);
	if (chosen == EGL_FALSE)
	{
		fprintf(stderr, "eglChooseConfig() returned EGL_FALSE.\n");
		return EGL_FALSE;
	}
	if (numConfigs<1)
	{
		fprintf(stderr, "eglChooseConfig() yielded %d configurations.\n", numConfigs);
		return EGL_FALSE;
	}

	// Create a surface
	egl_srf = eglCreateWindowSurface(egl_dpy, config, native_win, NULL);
	if ( egl_srf == EGL_NO_SURFACE )
	{
		fprintf(stderr, "eglCreateWindowSurface() returned EGL_NO_SURFACE.\n");
		return EGL_FALSE;
	}

	// Create a GL context
	EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE, EGL_NONE };
	egl_ctx = eglCreateContext(egl_dpy, config, EGL_NO_CONTEXT, contextAttribs );
	if ( egl_ctx == EGL_NO_CONTEXT )
	{
		fprintf(stderr, "eglCreateContext() returned EGL_NO_CONTEXT.\n");
		return EGL_FALSE;
	}

	// Make the context current
	if ( !eglMakeCurrent(egl_dpy, egl_srf, egl_srf, egl_ctx) )
	{
		fprintf(stderr, "eglMakeCurrent() returned EGL_FALSE.\n");
		return EGL_FALSE;
	}

	return EGL_TRUE;
}


static void draw()
{
	static int16_t red=0x20, grn=0x70, blu=0xa0;
	static int16_t dr=1, db=-1;

	glClearColor(red / 255.0f, grn / 255.0f, blu / 255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	red += dr;
	blu += db;
	if (red<0 || red>0xff) dr = -dr;
	if (blu<0 || blu>0xff) db = -db;
}


// Wayland helper funcs.

static int connect_to_wayland()
{
	native_dpy = wl_display_connect(0);
	if (!native_dpy)
	{
		fprintf(stderr, "wl_display_connect(0) return 0.\n");
		exit(1);
	}

	struct wl_registry *wl_registry = wl_display_get_registry(native_dpy);
	void* data = 0;
	wl_registry_add_listener(wl_registry, &registry_listener, data);
	wl_display_dispatch(native_dpy);
	wl_display_roundtrip(native_dpy);

	if (!compositor)
		fprintf(stderr, "Wayland server did not provide us with a compositor.\n");
	if (!wm_base)
		fprintf(stderr, "Wayland server did not provide us with a WM Base.\n");
	return (compositor && wm_base) ? 1 : 0;
}


static void cleanup_resources()
{
	eglDestroySurface(egl_dpy, egl_srf);
	egl_srf = 0;
	wl_egl_window_destroy(native_win);
	native_win = 0;
	xdg_toplevel_destroy(xdg_toplevel);
	xdg_toplevel = 0;
	xdg_surface_destroy(xdg_surface);
	xdg_surface = 0;
	wl_surface_destroy(surface);
	surface = 0;
	eglDestroyContext(egl_dpy, egl_ctx);
	egl_ctx = 0;
}


int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		fprintf(stderr, "Usage: /dev/video0 NV12\n");
		exit(1);
	}
	assert(argc==3);
	const char* devname = argv[1];
	const char* fourcc = argv[2];

	// First order of business:
	// Make sure we have a display, a compositor and a WM Base.
	const int connected = connect_to_wayland();
	if (!connected)
	{
		fprintf(stderr, "Giving up.\n");
		exit(2);
	}

	// Make a surface that we can draw into.
	surface = wl_compositor_create_surface(compositor);
	if (!surface)
	{
		fprintf(stderr, "The compositor failed to create a surface.\n");
		exit(3);
	}

	const uint32_t format = (fourcc[0]<<0) | (fourcc[1]<<8) | (fourcc[2]<<16) | (fourcc[3]<<24);
	int vr = setup_video(devname, format, 1);
	assert(vr>=0);
	fprintf(stderr, "v4l2 connected.\n");

	create_dma_buffers();

	xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	assert(xdg_surface);
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
	assert(xdg_toplevel);
	xdg_toplevel_set_title(xdg_toplevel, "Wayland EGL example");
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	wl_surface_commit(surface);

	// Create a native window, and make it opaque.
	region = wl_compositor_create_region(compositor);
	wl_region_add(region, 0, 0, winw, winh);
	wl_surface_set_opaque_region(surface, region);
	native_win = wl_egl_window_create(surface, winw, winh);
	assert(native_win != EGL_NO_SURFACE);

	// To do the drawing, we need an OpenGLES context.
	CreateEGLContext();

	// Main loop.
	while (!done)
	{
		wl_display_dispatch_pending(native_dpy);
		draw();
		eglSwapBuffers(egl_dpy, egl_srf);
	}

	cleanup_resources();

	wl_display_disconnect(native_dpy);
	native_dpy = 0;
	fprintf(stderr, "disconnected from wayland server.\n");

	exit(0);
}

