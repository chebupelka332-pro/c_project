# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = 

# Файлы
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = $(BIN_DIR)/huffman

# Цели по умолчанию
all: $(TARGET)

# Создание bin/huffman
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "✅ Build complete."

# Компиляция .c в .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Создание каталога obj при необходимости
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# Очистка
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Прогон программы
run: all
	./$(TARGET)

# Отладка
debug: CFLAGS += -g -O0
debug: clean all

.PHONY: all clean run debug
