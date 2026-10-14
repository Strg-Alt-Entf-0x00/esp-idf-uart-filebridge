#!/bin/bash
################################################################################
# ESP-IDF UART Filebridge - Multi-Target Build Script
#
# Builds firmware for all 7 ESP32 variants and generates size report.
# Useful for validating cross-platform compatibility and tracking binary sizes.
#
# Usage:
#   ./tools/build_all_targets.sh
#
# Output:
#   build_output/<target>_uart_filebridge.bin - Binary for each target
#   build_output/BUILD_REPORT.txt - Size summary table
#
# Requirements:
#   - ESP-IDF environment activated (. $IDF_PATH/export.sh)
#   - Run from repository root or examples/basic_transfer directory
#
# Date: 2026-10-13
# Version: 2.0.0
################################################################################

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ESP32 target variants
TARGETS=(
    "esp32"
    "esp32s2"
    "esp32s3"
    "esp32c3"
    "esp32c6"
    "esp32h2"
    "esp32p4"
)

# Working directory (assume script runs from repo root or examples/basic_transfer)
if [ -f "idf_component.yml" ]; then
    # Running from examples/basic_transfer
    BUILD_DIR="."
    PROJECT_NAME="basic_transfer"
elif [ -f "examples/basic_transfer/idf_component.yml" ]; then
    # Running from repo root
    BUILD_DIR="examples/basic_transfer"
    PROJECT_NAME="basic_transfer"
else
    echo -e "${RED}Error: Cannot find example project. Run from repo root or examples/basic_transfer${NC}"
    exit 1
fi

# Output directory
OUTPUT_DIR="${BUILD_DIR}/build_output"
mkdir -p "$OUTPUT_DIR"

# Report file
REPORT_FILE="${OUTPUT_DIR}/BUILD_REPORT.txt"
echo "ESP-IDF UART Filebridge - Multi-Target Build Report" > "$REPORT_FILE"
echo "Date: $(date)" >> "$REPORT_FILE"
echo "ESP-IDF Version: $(idf.py --version | head -n1)" >> "$REPORT_FILE"
echo "========================================" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

# Build summary arrays
declare -A BUILD_STATUS
declare -A BINARY_SIZES
declare -A BUILD_TIMES

# Function to format bytes as human-readable
format_size() {
    local size=$1
    if [ $size -lt 1024 ]; then
        echo "${size} B"
    elif [ $size -lt 1048576 ]; then
        echo "$((size / 1024)) KB"
    else
        echo "$((size / 1048576)) MB"
    fi
}

# Function to build for one target
build_target() {
    local target=$1
    local start_time=$(date +%s)
    
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}Building for: ${target}${NC}"
    echo -e "${BLUE}========================================${NC}"
    
    cd "$BUILD_DIR"
    
    # Set target
    echo -e "${YELLOW}Setting target to ${target}...${NC}"
    if ! idf.py set-target "$target" > /dev/null 2>&1; then
        echo -e "${RED}Failed to set target ${target}${NC}"
        BUILD_STATUS[$target]="FAILED (set-target)"
        return 1
    fi
    
    # Clean previous build
    echo -e "${YELLOW}Cleaning previous build...${NC}"
    idf.py fullclean > /dev/null 2>&1 || true
    
    # Build
    echo -e "${YELLOW}Building ${target}...${NC}"
    if idf.py build 2>&1 | tee "${OUTPUT_DIR}/${target}_build.log"; then
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        BUILD_TIMES[$target]=$duration
        
        # Find binary
        local binary_path="build/${PROJECT_NAME}.bin"
        if [ -f "$binary_path" ]; then
            local size=$(stat -c%s "$binary_path" 2>/dev/null || stat -f%z "$binary_path" 2>/dev/null)
            BINARY_SIZES[$target]=$size
            
            # Copy binary to output
            cp "$binary_path" "${OUTPUT_DIR}/${target}_uart_filebridge.bin"
            
            echo -e "${GREEN}✓ Build successful for ${target}${NC}"
            echo -e "${GREEN}  Binary size: $(format_size $size)${NC}"
            echo -e "${GREEN}  Build time: ${duration}s${NC}"
            BUILD_STATUS[$target]="SUCCESS"
        else
            echo -e "${RED}✗ Binary not found for ${target}${NC}"
            BUILD_STATUS[$target]="FAILED (no binary)"
            return 1
        fi
    else
        echo -e "${RED}✗ Build failed for ${target}${NC}"
        BUILD_STATUS[$target]="FAILED (compilation)"
        return 1
    fi
    
    cd - > /dev/null
}

# Main build loop
echo -e "${BLUE}Starting multi-target build...${NC}"
echo -e "${BLUE}Output directory: ${OUTPUT_DIR}${NC}"
echo ""

TOTAL_SUCCESS=0
TOTAL_FAILED=0

for target in "${TARGETS[@]}"; do
    if build_target "$target"; then
        ((TOTAL_SUCCESS++))
    else
        ((TOTAL_FAILED++))
    fi
    echo ""
done

# Generate summary report
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Build Summary${NC}"
echo -e "${BLUE}========================================${NC}"

{
    echo "Build Summary"
    echo "============="
    echo ""
    echo "Total Targets: ${#TARGETS[@]}"
    echo "Successful:    ${TOTAL_SUCCESS}"
    echo "Failed:        ${TOTAL_FAILED}"
    echo ""
    echo "Target-by-Target Results"
    echo "========================"
    echo ""
    printf "%-12s | %-10s | %-12s | %-10s\n" "Target" "Status" "Binary Size" "Build Time"
    echo "-------------+------------+--------------+-----------"
} >> "$REPORT_FILE"

for target in "${TARGETS[@]}"; do
    local status="${BUILD_STATUS[$target]}"
    local size="${BINARY_SIZES[$target]:-0}"
    local time="${BUILD_TIMES[$target]:-0}"
    
    if [ "$status" == "SUCCESS" ]; then
        local size_str=$(format_size $size)
        printf "%-12s | ${GREEN}%-10s${NC} | %-12s | %-10s\n" "$target" "$status" "$size_str" "${time}s"
        printf "%-12s | %-10s | %-12s | %-10s\n" "$target" "$status" "$size_str" "${time}s" >> "$REPORT_FILE"
    else
        printf "%-12s | ${RED}%-10s${NC} | %-12s | %-10s\n" "$target" "$status" "N/A" "N/A"
        printf "%-12s | %-10s | %-12s | %-10s\n" "$target" "$status" "N/A" "N/A" >> "$REPORT_FILE"
    fi
done

echo "" >> "$REPORT_FILE"
echo "Binary Size Statistics" >> "$REPORT_FILE"
echo "======================" >> "$REPORT_FILE"

if [ ${TOTAL_SUCCESS} -gt 0 ]; then
    # Calculate statistics
    local total_size=0
    local min_size=999999999
    local max_size=0
    
    for target in "${TARGETS[@]}"; do
        if [ "${BUILD_STATUS[$target]}" == "SUCCESS" ]; then
            local size=${BINARY_SIZES[$target]}
            total_size=$((total_size + size))
            [ $size -lt $min_size ] && min_size=$size
            [ $size -gt $max_size ] && max_size=$size
        fi
    done
    
    local avg_size=$((total_size / TOTAL_SUCCESS))
    
    echo "Minimum: $(format_size $min_size)" >> "$REPORT_FILE"
    echo "Maximum: $(format_size $max_size)" >> "$REPORT_FILE"
    echo "Average: $(format_size $avg_size)" >> "$REPORT_FILE"
    
    echo ""
    echo -e "${BLUE}Binary Size Statistics:${NC}"
    echo -e "  Minimum: $(format_size $min_size)"
    echo -e "  Maximum: $(format_size $max_size)"
    echo -e "  Average: $(format_size $avg_size)"
fi

echo "" >> "$REPORT_FILE"
echo "Output Files" >> "$REPORT_FILE"
echo "============" >> "$REPORT_FILE"
echo "Binaries:   ${OUTPUT_DIR}/<target>_uart_filebridge.bin" >> "$REPORT_FILE"
echo "Build logs: ${OUTPUT_DIR}/<target>_build.log" >> "$REPORT_FILE"

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Build complete!${NC}"
echo -e "${BLUE}Report saved to: ${REPORT_FILE}${NC}"
echo -e "${BLUE}Binaries in: ${OUTPUT_DIR}/${NC}"
echo -e "${BLUE}========================================${NC}"

# Exit with error if any builds failed
if [ ${TOTAL_FAILED} -gt 0 ]; then
    echo -e "${RED}Warning: ${TOTAL_FAILED} target(s) failed to build${NC}"
    exit 1
fi

exit 0
