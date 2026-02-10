#!/bin/bash

# --- Configuration: compression quality ---
JPEG_QUALITY=82      # JPEG compression (0-100)
PNG_COMPRESSION=9    # PNG compression level (0-9)
WEBP_QUALITY=80      # WebP compression (0-100)

# --- Directories ---
SAVE_DIR="./save"
SERVE_DIR="./serve"
ORIGINAL_DIR="./original"

# --- Ensure directories exist ---
mkdir -p "$SAVE_DIR"
mkdir -p "$SERVE_DIR"
mkdir -p "$ORIGINAL_DIR"

echo "Processing images from '$SAVE_DIR' into '$SERVE_DIR'..."
echo "Original files will be moved to '$ORIGINAL_DIR'..."

# --- Process JPEG/JPG ---
for img in "$SAVE_DIR"/*.{jpg,jpeg}; do
    [ -f "$img" ] || continue
    filename=$(basename "$img")
    echo "Compressing JPEG: $filename"
    mogrify -path "$SERVE_DIR" -strip -interlace Plane -quality "$JPEG_QUALITY" "$img"
    mv "$img" "$ORIGINAL_DIR/$filename"
done

# --- Process PNG ---
for img in "$SAVE_DIR"/*.png; do
    [ -f "$img" ] || continue
    filename=$(basename "$img")
    echo "Compressing PNG: $filename"
    mogrify -path "$SERVE_DIR" -strip -define png:compression-level="$PNG_COMPRESSION" "$img"
    mv "$img" "$ORIGINAL_DIR/$filename"
done

# --- Process WebP ---
for img in "$SAVE_DIR"/*.webp; do
    [ -f "$img" ] || continue
    filename=$(basename "$img")
    echo "Compressing WebP: $filename"
    mogrify -path "$SERVE_DIR" -strip -quality "$WEBP_QUALITY" "$img"
    mv "$img" "$ORIGINAL_DIR/$filename"
done

# --- Process SVG (copy only, then archive original) ---
for img in "$SAVE_DIR"/*.svg; do
    [ -f "$img" ] || continue
    filename=$(basename "$img")
    echo "Copying SVG: $filename"
    cp "$img" "$SERVE_DIR/$filename"
    mv "$img" "$ORIGINAL_DIR/$filename"
done

echo "All images processed."
echo "Compressed files are in '$SERVE_DIR'."
echo "Original files are archived in '$ORIGINAL_DIR'."
