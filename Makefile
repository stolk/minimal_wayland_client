CFLAGS = -g -Wall -Wextra

PROTOCOL=/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml

OBJS = \
minimal_wayland_client.o \
xdg-shell-protocol.o


all: xdg-shell-client-protocol.h minimal_wayland_client


minimal_wayland_client: $(OBJS)
	$(CC) -o minimal_wayland_client $(OBJS) -lwayland-client -lwayland-egl -lEGL -lGLESv2


xdg-shell-protocol.c: $(PROTOCOL)
	wayland-scanner private-code < $< > $@

xdg-shell-client-protocol.h: $(PROTOCOL)
	wayland-scanner client-header < $< > $@


