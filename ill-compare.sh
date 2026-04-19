#!/bin/bash

# Use first argument as target directory, default to current directory if empty
TARGET_DIR="${1:-.}"

# Check if the provided path is actually a directory
if [ ! -d "$TARGET_DIR" ]; then
    echo "Error: '$TARGET_DIR' is not a valid directory."
    echo "Usage: $0 [path/to/bitcode/files]"
    exit 1
fi

echo "================================================================"
printf "%-20s | %-10s | %-10s | %-10s\n" "Bitcode Pair" "m2r" "opt" "Diff"
echo "================================================================"

# Find all files ending in -m2r.bc
find "$TARGET_DIR" -maxdepth 1 -name "*-m2r.bc" | sort | while read -r base_bc; do
    
    # Identify the prefix (everything before -m2r.bc)
    prefix="${base_bc%-m2r.bc}"
    opt_bc="${prefix}-opt.bc"
    
    # Extract clean filename for display
    display_name=$(basename "$prefix")

    # Check if the optimized counterpart exists
    if [ ! -f "$opt_bc" ]; then
        continue
    fi

    # Run lli with interpreter and stats
    # 2>&1 merges stderr (where stats live) into stdout so we can grep it
    m2r_stats=$(lli -stats -force-interpreter "$base_bc" 2>&1 >/dev/null)
    opt_stats=$(lli -stats -force-interpreter "$opt_bc" 2>&1 >/dev/null)

    # Extract the digit count
    m2r_count=$(echo "$m2r_stats" | grep "instructions executed" | awk '{print $1}' | tr -d ',')
    opt_count=$(echo "$opt_stats" | grep "instructions executed" | awk '{print $1}' | tr -d ',')

    # Default to 0 if extraction fails
    m2r_count=${m2r_count:-0}
    opt_count=${opt_count:-0}

    # Calculate difference (positive means opt is better/smaller)
    diff=$((m2r_count - opt_count))

    # Colorize the output: Red if opt is higher (your current issue), Green if lower
    if [ "$diff" -lt 0 ]; then
        # Red text for regression
        printf "%-20s | %-10s | %-10s | \e[31m%10s\e[0m\n" "$display_name" "$m2r_count" "$opt_count" "$diff"
    else
        # Green text for improvement
        printf "%-20s | %-10s | %-10s | \e[32m%10s\e[0m\n" "$display_name" "$m2r_count" "$opt_count" "$diff"
    fi
done

echo "================================================================"