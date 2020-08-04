#
# First stage - builder
#
FROM debian:10-slim AS builder

ENV DEBIAN_FRONTEND noninteractive
ENV LC_ALL C.UTF-8

# This allows you to use a local Debian mirror
ARG APT_URL=http://deb.debian.org/debian/
ARG MAKE_FLAGS=-j2

RUN sed -i "s%http://deb.debian.org/debian/%${APT_URL}%" /etc/apt/sources.list \
    && apt-get update && apt-get -y upgrade \
    && apt-get install -y protobuf-compiler libh2o-dev libcurl4-openssl-dev \
           libssl-dev libprotobuf-dev libh2o-evloop-dev libwslay-dev \
           libeigen3-dev libzstd-dev libfmt-dev libncurses-dev \
           make gcc g++ git build-essential curl autoconf automake help2man

# Build
ADD . /galmon-src/
RUN cd /galmon-src/ \
    && make $MAKE_FLAGS \
    && prefix=/galmon make install

#
# Second stage - contains just the binaries
#
FROM debian:10-slim
RUN apt-get update && apt-get -y upgrade \
    && apt-get install -y libcurl4 libssl1.1 libprotobuf17 libh2o-evloop0.13 \
           libncurses6 \
    && apt-get -y clean \
    && rm -rf /var/lib/apt/lists/*
COPY --from=builder /galmon/ /galmon/
ENV PATH=/galmon/bin:${PATH}
ENV LC_ALL C.UTF-8
WORKDIR /galmon/bin
