FROM --platform=linux/amd64 ubuntu:20.04 as builder

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential clang

COPY . /repo
WORKDIR /repo/C/SysLib
ENV DESTDIR=/install
RUN make -j8
RUN make install
WORKDIR /repo
RUN make -j8
RUN make install
RUN clang++ /repo/mayhem/fuzz_TrimDotParts.cpp -fsanitize=fuzzer,address -L /repo/C/SysLib/bin/Linux.x86_64/ -lSysLib -o /fuzz

FROM ubuntu:20.04 as package
COPY --from=builder /install /install
COPY --from=builder /fuzz /fuzz_TrimDotParts
