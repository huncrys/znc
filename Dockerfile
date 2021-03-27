# syntax=docker/dockerfile:1

FROM alpine:3.24 AS base

FROM base AS builder

ARG VERSION_EXTRA=""

ARG CMAKEFLAGS="-DVERSION_EXTRA=${VERSION_EXTRA} -DCMAKE_INSTALL_PREFIX=/usr/local -DWANT_CYRUS=YES -DWANT_PERL=YES -DWANT_PYTHON=YES -DWANT_TCL=YES"
ARG MAKEFLAGS=""

RUN --mount=type=cache,target=/var/cache/apk \
  echo "**** install build packages ****" && \
  apk add -uU \
    argon2-dev \
    autoconf \
    automake \
    boost-dev \
    build-base \
    c-ares-dev \
    cmake \
    cyrus-sasl-dev \
    gettext-dev \
    icu-dev \
    openssl-dev \
    perl-dev \
    python3-dev \
    swig \
    tcl-dev

COPY . /znc-src

WORKDIR /znc-src/build

RUN \
  echo "**** compile znc ****" && \
  cmake .. ${CMAKEFLAGS} && \
  make -j"$(getconf _NPROCESSORS_ONLN)" $MAKEFLAGS && \
  make -j"$(getconf _NPROCESSORS_ONLN)" DESTDIR=/tmp/znc install

SHELL ["/bin/ash", "-o", "pipefail", "-c"]

RUN \
  echo "**** determine runtime packages ****" && \
  scanelf --needed --nobanner /tmp/znc/usr/local/bin/znc /tmp/znc/usr/local/lib/znc/ \
    | awk '{ gsub(/,/, "\nso:", $2); print "so:" $2 }' \
    | sort -u \
    | xargs -r apk info --installed \
    | sort -u \
    >> /tmp/znc/packages

FROM base

COPY --from=builder /tmp/znc/usr/ /usr/

# hadolint ignore=SC2086
RUN \
  --mount=type=cache,target=/var/cache/apk \
  --mount=type=bind,from=builder,source=/tmp/znc/packages,target=/packages \
  echo "**** install runtime packages ****" && \
  RUNTIME_PACKAGES=$(echo $(cat /packages)) && \
  apk add -uU \
    oidentd \
    su-exec \
    tini \
    ${RUNTIME_PACKAGES}

RUN adduser -S znc -h /znc-data && addgroup -S znc

COPY docker-entrypoint.sh /entrypoint.sh
COPY docker-oidentd.conf /etc/oidentd.conf

VOLUME /znc-data

ENTRYPOINT ["/sbin/tini", "--", "/entrypoint.sh"]
