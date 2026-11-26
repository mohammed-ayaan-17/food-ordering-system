FROM gcc:13-bookworm AS builder
WORKDIR /app
COPY server.c .
RUN gcc -O2 -Wall -Wextra server.c -o server

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=builder /app/server .
COPY public ./public
ENV PORT=8080
EXPOSE 8080
CMD ["./server"]
