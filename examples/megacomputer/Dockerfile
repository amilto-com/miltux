# MiLTuX Mega-Computer Node
#
# Two-stage build so the final image contains only the miltux binary and the
# Alpine runtime — total image size ~7 MB.
#
# Stage 1 – build on Alpine with GCC
FROM alpine:3.19 AS builder

RUN apk add --no-cache gcc musl-dev make

WORKDIR /build

# Copy the whole repository so make can find all sources
COPY . .

RUN make clean && make PREFIX=/usr/local install

# Stage 2 – minimal runtime image
FROM alpine:3.19

COPY --from=builder /usr/local/bin/miltux /usr/local/bin/miltux
COPY examples/megacomputer/entrypoint.sh  /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Default MiLTuX peer port
EXPOSE 7070

ENTRYPOINT ["/entrypoint.sh"]
