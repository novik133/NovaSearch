# NovaSearch Panel Plugin Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC
LDFLAGS = -shared

# Package config for dependencies
PKG_CONFIG = pkg-config
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS = $(shell $(PKG_CONFIG) --libs gtk+-3.0)
XFCE_CFLAGS = $(shell $(PKG_CONFIG) --cflags libxfce4panel-2.0 2>/dev/null || echo "")
XFCE_LIBS = $(shell $(PKG_CONFIG) --libs libxfce4panel-2.0 2>/dev/null || echo "")
SQLITE_CFLAGS = $(shell $(PKG_CONFIG) --cflags sqlite3)
SQLITE_LIBS = $(shell $(PKG_CONFIG) --libs sqlite3)
# Keybinder flags (manual since pkg-config may not be available)
KEYBINDER_CFLAGS = -I/usr/include/keybinder-3.0
KEYBINDER_LIBS = -lkeybinder-3.0

ALL_CFLAGS = $(CFLAGS) $(GTK_CFLAGS) $(XFCE_CFLAGS) $(SQLITE_CFLAGS) $(KEYBINDER_CFLAGS)
ALL_LIBS = $(GTK_LIBS) $(XFCE_LIBS) $(SQLITE_LIBS) $(KEYBINDER_LIBS)

# Source files
SRC_DIR = src
BUILD_DIR = build
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Output
TARGET = libnovasearch-panel.so

# Test configuration
TEST_DIR = tests
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TEST_DIR)/%.c=$(BUILD_DIR)/%.o)
TEST_TARGET = $(BUILD_DIR)/test_runner
THEFT_LIBS = -ltheft

.PHONY: all clean test install

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(ALL_LIBS) -o $@

test: $(BUILD_DIR) $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJECTS) $(filter-out $(BUILD_DIR)/main.o, $(OBJECTS))
	$(CC) $(TEST_OBJECTS) $(filter-out $(BUILD_DIR)/main.o, $(OBJECTS)) $(ALL_LIBS) $(THEFT_LIBS) -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET)
	install -D $(TARGET) $(DESTDIR)/usr/lib/xfce4/panel/plugins/$(TARGET)
	install -D novasearch-panel.desktop $(DESTDIR)/usr/share/xfce4/panel/plugins/novasearch-panel.desktop
