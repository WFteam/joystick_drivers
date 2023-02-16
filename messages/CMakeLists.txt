#
# Glydways 2023
# https://glydways.com/
#

glyd_add_rbuf_library(
	joy_linux_messages ALIAS third_party::joy_linux::messages
	SOURCES
    joy.rbuf
    joy_feedback.rbuf
    joy_linux_config.rbuf
	LIBRARIES
)

