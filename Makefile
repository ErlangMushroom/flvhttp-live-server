.PHONY : all clean

CROSS_COMPILE :=

CC := $(CROSS_COMPILE)g++

TARGET := http_actor
SRCS := $(wildcard *.cc)
OBJS := $(patsubst %.cc, %.o, $(SRCS))

INCLUDE_FLAGS += -I/home/matt/Work/source/cxx/awesome/actor-framework/libcaf_core -I/home/matt/Work/source/cxx/awesome/actor-framework/libcaf_io

CXXFLAGS += -g -std=c++11 -O0 $(INCLUDE_FLAGS)
LDFLAGS := -L/home/matt/Work/source/cxx/awesome/actor-framework/build/lib -L./http-parser

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) -lcaf_io -lcaf_core -lhttp_parser

clean :
	-rm -rf *.o $(TARGET)
