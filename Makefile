.PHONY : all clean

CROSS_COMPILE :=

CC := $(CROSS_COMPILE)g++

TARGET := http_actor
SRCS := $(wildcard *.cc)
OBJS := $(patsubst %.cc, %.o, $(SRCS))

INCLUDE_FLAGS += -I../../../libcaf_core -I../../../libcaf_io

CXXFLAGS += -g -std=c++11 -O0 $(INCLUDE_FLAGS)
LDFLAGS := -L../../lib

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) -lcaf_io -lcaf_core -lhttp_parser

clean :
	-rm -rf *.o $(TARGET)
