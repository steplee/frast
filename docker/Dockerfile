FROM frast:latest AS frastDev

COPY --from=frastDev /usr/local/bin/frast* /usr/local/bin
COPY --from=frastDev /usr/lib/x86_64-linux-gnu/libopencv_imgproc.so /usr/lib/x86_64-linux-gnu/libopencv_core.so /usr/lib/x86_64-linux-gnu/libopencv_highgui.so /usr/lib/x86_64-linux-gnu/libopencv_imgcodecs.so /usr/local/lib/libfrast.a /usr/local/lib/libfrastVk.so /usr/local/lib
