FROM base/devel:latest AS builder
ADD . /data
WORKDIR /data
RUN pacman -Sy --noconfirm cmake git && git submodule update --init && mkdir build && cd build && cmake .. && make && cd .. && mkdir dist && cd dist && cp ../build/apid-test* .

FROM base/archlinux:latest AS packager
COPY --from=builder /data/dist /data
