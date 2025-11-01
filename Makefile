# Simple Makefile for macOS (Apple Silicon or Intel)
# Builds the OpenGL game to an executable named `main_program`

# You can override these from the command line if needed, e.g.:
# make GLFW_INCLUDE_PATH=/usr/local/include GLFW_LIB_PATH=/usr/local/lib

CC := clang
CFLAGS := -O2 -Wall -Wextra -std=c11

# Paths
GLAD_INC := lib/GLAD
SRC := main.c src/glad.c
OBJ := $(SRC:.c=.o)

# Try common Homebrew prefixes by default
GLFW_INCLUDE_PATH ?= /opt/homebrew/include
GLFW_LIB_PATH ?= /opt/homebrew/lib

INCLUDES := -I$(GLAD_INC) -I$(GLFW_INCLUDE_PATH)
LDFLAGS := -L$(GLFW_LIB_PATH) -lglfw -framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo

TARGET := main_program

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)
