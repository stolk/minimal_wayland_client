# Minimal Wayland Client

Minimal code for a wayland client that draws to an opaque window using OpenGL-ES.

This code also lists the supported pixelformats by listening to dmabuf protocol.

## Supported formats

### Weston

The weston compositor supports NV12:

```
dmabuf listener found format AB4H
dmabuf listener found format XB4H
dmabuf listener found format AB48
dmabuf listener found format XB48
dmabuf listener found format AR30
dmabuf listener found format XR30
dmabuf listener found format AB30
dmabuf listener found format AR24
dmabuf listener found format AB24
dmabuf listener found format XR24
dmabuf listener found format XB24
dmabuf listener found format AR15
dmabuf listener found format RG16
dmabuf listener found format YUV9
dmabuf listener found format YU11
dmabuf listener found format YU12
dmabuf listener found format YU16
dmabuf listener found format YU24
dmabuf listener found format YVU9
dmabuf listener found format YV11
dmabuf listener found format YV12
dmabuf listener found format YV16
dmabuf listener found format YV24
dmabuf listener found format NV12
dmabuf listener found format NV16
dmabuf listener found format XYUV
dmabuf listener found format YUYV
dmabuf listener found format UYVY
```

### Mutter

The mutter compositor does not support NV12 (nor any YUV format.):
```
dmabuf listener found format AR24
dmabuf listener found format AB24
dmabuf listener found format XR24
dmabuf listener found format XB24
dmabuf listener found format AR30
dmabuf listener found format AB30
dmabuf listener found format XR30
dmabuf listener found format RG16
dmabuf listener found format AB4H
dmabuf listener found format XB4H
```


## Dependencies

wayland-protocols

wayland-scanner (from libwayland-bin)

libwayland-dev

libegl-dev

libgles2-mesa-dev

libxdg-basedir-dev

## License

MIT

## Author

Bram Stolk (b.stolk@gmail.com)

