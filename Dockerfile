FROM base/devel:latest AS builder
ADD . /data
WORKDIR /data
RUN pacman -Sy --noconfirm cmake && mkdir build && cd build && cmake .. && make && cd .. && mkdir dist && cd dist && cp ../build/apid-test* .

FROM base/archlinux:latest AS packager
COPY --from=builder /data/dist /data