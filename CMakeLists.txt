cmake_minimum_required(VERSION 3.27)
project(redis C)

set(CMAKE_C_STANDARD 11)

add_executable(redis main.c
        redis.c
        redis.h
        serde.h
        serde.c
        uthash.h
        utils.c
        utils.h
        socket_utils.c
        socket_utils.h
)
