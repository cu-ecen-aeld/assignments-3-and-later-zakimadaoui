#!/bin/bash

# Check if the required arguments are provided
if [ $# -ne 2 ]; then
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

writefile=$1
writestr=$2

# Create the directory if it doesn't exist
mkdir -p "$(dirname "$writefile")"

# Write to the file
echo "$writestr" > "$writefile"

# Check if writing was successful
if [ $? -ne 0 ]; then
    echo "Error: Failed to write to $writefile"
    exit 1
fi