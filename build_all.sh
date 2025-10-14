#!/bin/bash

# Script to compile all firmware binaries
# Generates: factory, OTA and individual binaries

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BOARD="${BOARD:-NERDQAXEPLUS2}"
BIGSCREEN="${BIGSCREEN:-1}"  # Enable bigscreen by default (set to 0 for standard screen)
OUTPUT_DIR="build"
FACTORY_BIN="esp-miner-factory-${BOARD}.bin"
OTA_BIN="esp-miner-NerdQAxe++.bin"
WWW_BIN="www.bin"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  ESP-Miner Build Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "Board: ${GREEN}${BOARD}${NC}"
if [ "$BIGSCREEN" = "1" ]; then
    echo -e "Display: ${GREEN}480x320 (bigscreen)${NC}"
else
    echo -e "Display: ${GREEN}320x170 (standard)${NC}"
fi
echo ""

# Step 1: Check if Docker is available
if command -v docker &> /dev/null; then
    USE_DOCKER=true
    echo -e "${GREEN}✓${NC} Docker detected"

    # Check if Docker image exists
    if ! docker images | grep -q "esp-idf-builder"; then
        echo -e "${YELLOW}⚠${NC} Docker image not found. Building..."
        cd docker
        ./build_docker.sh
        cd ..
    fi
else
    USE_DOCKER=false
    echo -e "${YELLOW}⚠${NC} Docker not detected, using local ESP-IDF"

    # Verify ESP-IDF is installed
    if [ -z "$IDF_PATH" ]; then
        echo -e "${RED}✗${NC} ESP-IDF is not configured. Please install ESP-IDF v5.3.x"
        echo "Or install Docker to use the containerized environment"
        exit 1
    fi
fi

echo ""
echo -e "${BLUE}Step 1/5: Configuring target ESP32-S3${NC}"
if [ "$USE_DOCKER" = true ]; then
    BOARD="$BOARD" BIGSCREEN="$BIGSCREEN" ./docker/idf-ci.sh set-target esp32-s3
else
    BOARD="$BOARD" BIGSCREEN="$BIGSCREEN" idf.py set-target esp32-s3
fi

echo ""
echo -e "${BLUE}Step 2/5: Compiling firmware and frontend...${NC}"
if [ "$USE_DOCKER" = true ]; then
    BOARD="$BOARD" BIGSCREEN="$BIGSCREEN" ./docker/idf-ci.sh build
else
    BOARD="$BOARD" BIGSCREEN="$BIGSCREEN" idf.py build
fi

echo ""
echo -e "${BLUE}Step 3/5: Renaming OTA firmware binary...${NC}"
cp "${OUTPUT_DIR}/esp-miner.bin" "${OUTPUT_DIR}/${OTA_BIN}"
echo "  Created ${OUTPUT_DIR}/${OTA_BIN}"

echo ""
echo -e "${BLUE}Step 4/5: Generating complete factory binary...${NC}"
./merge_bin.sh "${OUTPUT_DIR}/${FACTORY_BIN}"

echo ""
echo -e "${BLUE}Step 5/5: Final binaries ready in ${OUTPUT_DIR}/...${NC}"
echo "  - ${OUTPUT_DIR}/${OTA_BIN} (OTA firmware)"
echo "  - ${OUTPUT_DIR}/www.bin (web interface)"
echo "  - ${OUTPUT_DIR}/${FACTORY_BIN} (complete factory)"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  ✓ Build completed${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Generated binaries in ${OUTPUT_DIR}/:"
echo -e "  ${BLUE}OTA (firmware update):${NC}"
echo -e "    - ${OUTPUT_DIR}/${OTA_BIN} ($(du -h ${OUTPUT_DIR}/${OTA_BIN} | cut -f1))"
echo ""
echo -e "  ${BLUE}OTA (web interface update):${NC}"
echo -e "    - ${OUTPUT_DIR}/www.bin ($(du -h ${OUTPUT_DIR}/www.bin | cut -f1))"
echo ""
echo -e "  ${BLUE}Factory (complete flash):${NC}"
echo -e "    - ${OUTPUT_DIR}/${FACTORY_BIN} ($(du -h ${OUTPUT_DIR}/${FACTORY_BIN} | cut -f1))"
echo ""
echo -e "  ${BLUE}Other binaries in ${OUTPUT_DIR}/:${NC}"
echo -e "    - esp-miner.bin (original firmware binary)"
echo -e "    - bootloader/bootloader.bin"
echo -e "    - partition_table/partition-table.bin"
echo -e "    - ota_data_initial.bin"
echo ""
echo -e "${YELLOW}Usage:${NC}"
echo -e "  Factory flash:       esptool.py -p /dev/ttyACM0 -b 460800 write_flash 0x0 ${OUTPUT_DIR}/${FACTORY_BIN}"
echo -e "  OTA FW update:       Upload ${OUTPUT_DIR}/${OTA_BIN} from web interface"
echo -e "  OTA Web update:      Upload ${OUTPUT_DIR}/www.bin from web interface"
echo ""
