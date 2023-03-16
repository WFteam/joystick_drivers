glyd_add_library(
    joy_linux_node
    ALIAS
        third_party::joy_linux
    SOURCES
        joy_linux_node.cc
    LIBRARIES
        third_party::joy_linux::rbuf
		ark::pipeline
    )

add_subdirectory(rbuf)
