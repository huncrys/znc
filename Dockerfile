FROM alpine:3.20.3 AS base

FROM base AS builder

ARG VERSION_EXTRA=""

ARG CMAKEFLAGS="-DVERSION_EXTRA=${VERSION_EXTRA} -DCMAKE_INSTALL_PREFIX=/usr -DWANT_CYRUS=YES -DWANT_PERL=YES -DWANT_PYTHON=YES -DWANT_TCL=YES"
ARG MAKEFLAGS=""

LABEL org.label-schema.schema-version="1.0"
LABEL org.label-schema.vcs-url="https://oaklab.hu/crys/znc"
LABEL org.label-schema.url="https://znc.in"

RUN set -x \
    && adduser -S znc \
    && addgroup -S znc \
    && apk add --no-cache \
        argon2-libs \
        boost \
        build-base \
        cmake \
        cyrus-sasl \
        gettext \
        icu-dev \
        icu-data-full \
        openssl-dev \
        perl \
        python3 \
        su-exec \
        tini \
        tzdata \
    && apk add --no-cache --virtual build-dependencies \
        argon2-dev \
        boost-dev \
        cyrus-sasl-dev \
        perl-dev \
        python3-dev \
        icu-dev \
        openssl-dev \
        swig \
        gettext-dev \
        tcl-dev

COPY . /znc-src

RUN cd /znc-src \
    && mkdir build && cd build \
    && cmake .. ${CMAKEFLAGS} \
    && make -j`getconf _NPROCESSORS_ONLN` $MAKEFLAGS \
    && make -j`getconf _NPROCESSORS_ONLN` DESTDIR=/tmp/znc install

RUN \
 echo "**** determine runtime packages ****" && \
 scanelf --needed --nobanner /tmp/znc/usr/bin/znc \
    | awk '{ gsub(/,/, "\nso:", $2); print "so:" $2 }' \
    | sort -u \
    | xargs -r apk info --installed \
    | sort -u \
    >> /tmp/znc/packages

FROM base

COPY --from=builder /tmp/znc/usr/ /usr/
COPY --from=builder /tmp/znc/packages /packages

RUN \
 echo "**** install runtime packages ****" && \
 RUNTIME_PACKAGES=$(echo $(cat /packages)) && \
 apk add --no-cache \
    su-exec \
    ca-certificates \
    icu-data-full \
    tini \
    oidentd \
    python3 \
    tcl \
    perl \
    cyrus-sasl \
    icu \
    gettext \
    boost \
    ${RUNTIME_PACKAGES}

RUN adduser -S znc -h /znc-data && addgroup -S znc

COPY docker-entrypoint.sh /entrypoint.sh
COPY docker-oidentd.conf /etc/oidentd.conf

VOLUME /znc-data

ENTRYPOINT ["/sbin/tini", "--", "/entrypoint.sh"]
