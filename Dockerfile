FROM alpine:3.13 AS builder

LABEL org.label-schema.schema-version="1.0"
LABEL org.label-schema.vcs-ref=$VCS_REF
LABEL org.label-schema.vcs-url="https://github.com/znc/znc"
LABEL org.label-schema.build-date=$BUILD_DATE
LABEL org.label-schema.url="https://znc.in"

RUN set -x \
    && apk add --no-cache \
        build-base \
        cmake \
        boost-dev \
        cyrus-sasl-dev \
        perl-dev \
        python3-dev \
        icu-dev \
        libressl-dev \
        swig \
        gettext-dev \
        tcl-dev

ARG VERSION_EXTRA=""
ARG CMAKEFLAGS="-DVERSION_EXTRA=${VERSION_EXTRA} -DCMAKE_INSTALL_PREFIX=/usr -DWANT_CYRUS=YES -DWANT_PERL=YES -DWANT_PYTHON=YES -DWANT_TCL=YES"
ARG MAKEFLAGS=""
ARG BUILD_DATE
ARG VCS_REF

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

FROM alpine:3.13

COPY --from=builder /tmp/znc/usr/ /usr/
COPY --from=builder /tmp/znc/packages /packages

RUN \
 echo "**** install runtime packages ****" && \
 RUNTIME_PACKAGES=$(echo $(cat /packages)) && \
 apk add --no-cache \
    su-exec \
	ca-certificates \
    tini \
    oidentd \
	${RUNTIME_PACKAGES}

RUN adduser -S znc -h /znc-data && addgroup -S znc

COPY docker-entrypoint.sh /entrypoint.sh
COPY docker-oidentd.conf /etc/oidentd.conf

VOLUME /znc-data

ENTRYPOINT ["/sbin/tini", "--", "/entrypoint.sh"]
