CFLAGS = -g -Wall -Wextra

PROTOCOL_XDG=/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml

PROTOCOL_DMA=/usr/share/wayland-protocols/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml

OBJS = \
minimal_wayland_client.o \
xdg-shell-protocol.o \
linux-dma-protocol.o


all: xdg-shell-client-protocol.h minimal_wayland_client


minimal_wayland_client: $(OBJS)
	$(CC) -o minimal_wayland_client $(OBJS) -lwayland-client -lwayland-egl -lEGL -lGLESv2


xdg-shell-protocol.c: $(PROTOCOL_XDG)
	wayland-scanner private-code < $< > $@

xdg-shell-client-protocol.h: $(PROTOCOL_XDG)
	wayland-scanner client-header < $< > $@

linux-dma-protocol.h: $(PROTOCOL_DMA)
	wayland-scanner client-header < $< > $@

linux-dma-protocol.c: $(PROTOCOL_DMA)
	wayland-scanner private-code < $< > $@

clean:
	rm -f *.o

