#!/bin/bash

# Script to convert all images starting with "resized_" using convert_single.py

THEME="NerdQaxePlus2"
RAW_IMAGES_DIR="./Raw Images"
CONVERTER_SCRIPT="../../Converter Tool/convert_single.py"

# Check if the converter script exists
if [ ! -f "$CONVERTER_SCRIPT" ]; then
    echo "Error: Converter script not found at $CONVERTER_SCRIPT"
    exit 1
fi

# Check if Raw Images directory exists
if [ ! -d "$RAW_IMAGES_DIR" ]; then
    echo "Error: Raw Images directory not found at $RAW_IMAGES_DIR"
    exit 1
fi

# Loop through all PNG files starting with "resized_" in the Raw Images directory
for img in "$RAW_IMAGES_DIR"/resized_*.png; do
    if [ -f "$img" ]; then

        
        echo "Optimizing image..."
        pngquant --quality=60-80 "$img" -o "${img}_compressed"
        optipng -o7 "${img}_compressed"

        # Extract the base filename without "resized_" prefix and without .png extension
        filename=$(basename "${img}" .png)
        screenname="${filename#resized_}"

        echo "Converting $filename to $screenname..."
        
        # Run the converter
        python3 "$CONVERTER_SCRIPT" "$THEME" "${img}_compressed" "$screenname"
        
        if [ $? -eq 0 ]; then
            echo "✓ Successfully converted $screenname"
        else
            echo "✗ Failed to convert $screenname"
        fi
    fi
done

echo ""
echo "Conversion complete!"