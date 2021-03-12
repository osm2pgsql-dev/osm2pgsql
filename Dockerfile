FROM alpine as builder

RUN apk --update-cache add cmake make g++ boost-dev expat-dev bzip2-dev zlib-dev libpq proj-dev lua5.3-dev postgresql-dev
ADD . /opt/
RUN mkdir /opt/build
WORKDIR /opt/build
RUN cmake ..
RUN make
CMD ["./osm2pgsql"]
