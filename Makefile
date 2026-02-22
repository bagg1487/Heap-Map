CXX = g++
CXXFLAGS = -std=c++17 -I./include -I./third-party -I./third-party/imgui -I./third-party/imgui/backends -I/usr/include
LDFLAGS = -lzmq -lglfw -lGL -lpthread -ldl -lX11

SRC_DIR = src
BUILD_DIR = build
THIRD_PARTY_DIR = third-party

IMGUI_CORE = \
    $(THIRD_PARTY_DIR)/imgui/imgui.cpp \
    $(THIRD_PARTY_DIR)/imgui/imgui_draw.cpp \
    $(THIRD_PARTY_DIR)/imgui/imgui_tables.cpp \
    $(THIRD_PARTY_DIR)/imgui/imgui_widgets.cpp

IMGUI_BACKENDS = \
    $(THIRD_PARTY_DIR)/imgui/backends/imgui_impl_glfw.cpp \
    $(THIRD_PARTY_DIR)/imgui/backends/imgui_impl_opengl3.cpp

IMGUI_SOURCES = $(IMGUI_CORE) $(IMGUI_BACKENDS)

SOURCES = $(SRC_DIR)/main.cpp $(SRC_DIR)/gui.cpp $(SRC_DIR)/server.cpp

IMGUI_OBJECTS = $(patsubst $(THIRD_PARTY_DIR)/%.cpp,$(BUILD_DIR)/third-party/%.o,$(IMGUI_SOURCES))
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES)) $(IMGUI_OBJECTS)

TARGET = $(BUILD_DIR)/gps_server

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $^ -o $@ $(LDFLAGS)
	@echo "Build complete! Run ./$(TARGET)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/third-party/%.o: $(THIRD_PARTY_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	find . -name "*.o" -type f -delete
	@echo "Clean complete!"

run: $(TARGET)
	./$(TARGET)

debug: CXXFLAGS += -g -O0
debug: clean all

.PHONY: all clean run debug