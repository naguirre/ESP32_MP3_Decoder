
# Main Makefile. This is basically the same as a component makefile.
#
# This Makefile should, at the very least, just include $(SDK_PATH)/make/component_common.mk. By default,
# this will take the sources in the src/ directory, compile them and link them into
# lib(subdirectory_name).a in the build directory. This behaviour is entirely configurable,
# please read the ESP-IDF documents if you need to do this.
#


COMPONENT_SRCDIRS := squeezelite port
COMPONENT_OBJS := squeezelite/slimproto.o squeezelite/utils.o squeezelite/buffer.o squeezelite/stream.o squeezelite/decode.o squeezelite/faad.o squeezelite/mad.o squeezelite/resample.o squeezelite/process.o squeezelite/output.o squeezelite/output_pack.o port/output_esp32.o

COMPONENT_SUBMODULES += squeezelite
COMPONENT_ADD_INCLUDEDIRS += squeezelite ../mad/

DEPS = squeezelite/squeezelite.h squeezelite/slimproto.h

CFLAGS += -D__FreeRTOS__ -DLINKALL
