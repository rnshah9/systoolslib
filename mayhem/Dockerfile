FROM --platform=linux/amd64 ubuntu:20.04 as builder

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential

COPY . /repo
WORKDIR /repo/C/SysLib
ENV DESTDIR=/install
RUN make -j8
RUN make install
WORKDIR /repo
RUN make -j8
RUN make install

FROM ubuntu:20.04 as package
COPY --from=builder /install /install
