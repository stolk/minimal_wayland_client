CFLAGS = -g -Wall -Wextra

PROTOCOL_XDG=/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml

PROTOCOL_DMA=/usr/share/wayland-protocols/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml

OBJS0 = \
minimal_wayland_client.o \
xdg-shell-protocol.o \
linux-dma-protocol.o

OBJS1 = \
minimal_nv12.o \
xdg-shell-protocol.o \
linux-dma-protocol.o

all: xdg-shell-client-protocol.h linux-dma-protocol.h linux-dma-protocol.c minimal_wayland_client minimal_nv12

minimal_wayland_client: $(OBJS0)
	$(CC) -o minimal_wayland_client $(OBJS0) -lwayland-client -lwayland-egl -lEGL -lGLESv2

minimal_nv12: $(OBJS1)
	$(CC) -o minimal_nv12 $(OBJS1) -lwayland-client -lwayland-egl -lEGL -lGLESv2


xdg-shell-protocol.c: $(PROTOCOL_XDG)
	wayland-scanner private-code < $< > $@

xdg-shell-client-protocol.h: $(PROTOCOL_XDG)
	wayland-scanner client-header < $< > $@

linux-dma-protocol.h: $(PROTOCOL_DMA)
	wayland-scanner client-header < $< > $@

linux-dma-protocol.c: $(PROTOCOL_DMA)
	wayland-scanner private-code < $< > $@

clean:
	rm -f $(OBJS0) $(OBJS1)

run:	minimal_nv12
	#v4l2-ctl -d /dev/video0  --set-fmt-video=pixelformat=NV12,width=1920,height=1080 --verbose
	#./minimal_nv12 /dev/video0
	v4l2-ctl -d /dev/video0  --set-fmt-video=pixelformat=YUYV,width=1920,height=1080 --verbose
	./minimal_nv12 /dev/video0 YUYV

