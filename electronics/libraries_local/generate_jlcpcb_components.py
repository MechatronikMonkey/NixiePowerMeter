#!/usr/bin/env python3
"""
JLC2KiCadLib Batch Generator Script

This script reads a list of JLCPCB component numbers from a text file and 
generates KiCad library files for each component using the JLC2KiCadLib tool.
"""

import os
import sys
import subprocess
import argparse
import time

def read_component_list(file_path):
    """Read component numbers from a text file, one per line."""
    try:
        with open(file_path, 'r') as f:
            # Strip whitespace and filter out empty lines or comments
            components = [line.strip() for line in f.readlines()]
            components = [c for c in components if c and not c.startswith('#')]
        return components
    except FileNotFoundError:
        print(f"Error: Component list file '{file_path}' not found.")
        sys.exit(1)

def generate_components(components, output_dir=None, symbol_lib=None, footprint_lib=None):
    """Generate KiCad library files for each component using JLC2KiCadLib."""
    if not components:
        print("No components found in the list file.")
        return
    
    # Prepare command
    cmd = ["JLC2KiCadLib"] + components
    
    # Add output directory if specified
    if output_dir:
        # Make sure the output directory exists
        os.makedirs(output_dir, exist_ok=True)
        cmd.extend(["-dir", output_dir])
    
    # Set symbol library name if specified
    if symbol_lib:
        cmd.extend(["-symbol_lib", symbol_lib])
    
    # Set footprint library name if specified
    if footprint_lib:
        cmd.extend(["-footprint_lib", footprint_lib])
    
    print(f"Generating {len(components)} components...")
    
    try:
        # Run the command
        start_time = time.time()
        process = subprocess.run(cmd, capture_output=True, text=True)
        end_time = time.time()
        
        # Check for errors
        if process.returncode != 0:
            print(f"Error generating components: {process.stderr}")
            return False
        
        print(f"Successfully generated {len(components)} components in {end_time - start_time:.2f} seconds.")
        print(f"Output: {process.stdout}")
        return True
    except Exception as e:
        print(f"Failed to run JLC2KiCadLib: {str(e)}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Generate KiCad library files for JLCPCB components in batch.')
    parser.add_argument('-f', '--file', default='jlcpcb_parts_list.txt',
                        help='Path to the text file containing component numbers (default: jlcpcb_parts_list.txt)')
    parser.add_argument('-d', '--output-dir', 
                        help='Output directory for generated files (default: ./jlcpcb_lib)')
    parser.add_argument('-s', '--symbol-lib', default='symbols',
                        help='Name for the symbol library file (default: symbols)')
    parser.add_argument('-fp', '--footprint-lib', default='footprints',
                        help='Name for the footprint library (default: footprints)')
    
    args = parser.parse_args()
    
    # Get the current directory where the script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Use the file path from arguments or the default one in the script directory
    if os.path.isabs(args.file):
        file_path = args.file
    else:
        file_path = os.path.join(script_dir, args.file)
    
    # Set default output directory if not specified
    output_dir = args.output_dir
    if not output_dir:
        output_dir = os.path.join(script_dir, "jlcpcb_lib")
        print(f"No output directory specified, using default: {output_dir}")
    
    # Read component list
    components = read_component_list(file_path)
    print(f"Found {len(components)} components in {file_path}")
    
    # Generate components
    success = generate_components(
        components, 
        output_dir, 
        args.symbol_lib, 
        args.footprint_lib
    )
    
    if success:
        print("Component generation complete.")
    else:
        print("Component generation failed.")
        sys.exit(1)

if __name__ == "__main__":
    main()