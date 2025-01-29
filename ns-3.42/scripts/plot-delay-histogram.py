import xml.etree.ElementTree as ET
import matplotlib.pyplot as plt
import sys

def main():
    # Check if the file path is provided
    if len(sys.argv) != 2:
        print("Usage: python plot_histogram.py <path_to_histogram.xml>")
        sys.exit(1)

    # Get the file path from the command-line argument
    file_path = sys.argv[1]

    try:
        # Parse XML file
        tree = ET.parse(file_path)
        root = tree.getroot()

        # Extract bin data
        bins = []
        counts = []

        for bin_elem in root.findall("bin"):
            start = float(bin_elem.get("start"))
            count = int(bin_elem.get("count"))

            bins.append(start)
            counts.append(count)

        # Plot histogram
        plt.bar(bins, counts, width=bins[1] - bins[0] if len(bins) > 1 else 0.01, 
                align="edge", edgecolor="black")
        plt.xlabel("Delay (s)")
        plt.ylabel("Count")
        plt.title("Histogram of Delays")
        plt.grid(True)
        plt.show()

    except FileNotFoundError:
        print(f"Error: File '{file_path}' not found!")
        sys.exit(1)
    except ET.ParseError:
        print(f"Error: Failed to parse XML file '{file_path}'.")
        sys.exit(1)

if __name__ == "__main__":
    main()
