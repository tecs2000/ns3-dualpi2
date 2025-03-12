import os
import re
import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict
import seaborn as sns  # Import seaborn for improved aesthetics

sns.set_theme(style="whitegrid")  # Set the plot theme to whitegrid

ue_counts = [2, 5, 7, 10]  # Different UE scenarios
avg_queue_delays_base = []
avg_buffer_sizes_base = []
avg_mac_credits_base = []
aqm_avg_throughputs = []

avg_queue_delays_other = []
avg_buffer_sizes_other = []
avg_mac_credits_other = []
no_aqm_avg_throughputs = []

def extract_downlink_throughput(file_path):
    downlink_throughputs = []
    with open(file_path, "r") as file:
        lines = file.readlines()
        i = 0
        while i < len(lines):
            line = lines[i].strip()
            # Match downlink flows (from 7.0.0.0/8 to 1.0.0.0/8 or 2.0.0.0/8)
            flow_match = re.match(r"Flow \d+ \(7\.0\.0\.\d+:\d+ -> (1|2)\.0\.0\.\d+:\d+\)", line)
            if flow_match:
                # Look ahead for the Throughput line
                for j in range(i + 1, min(i + 5, len(lines))):  # Check next 5 lines for throughput
                    throughput_match = re.search(r"Throughput: ([\d.]+) Mbps", lines[j])
                    if throughput_match:
                        throughput = float(throughput_match.group(1))
                        downlink_throughputs.append(throughput)
                        break
            i += 1  # Move to the next line

    return np.mean(downlink_throughputs) if downlink_throughputs else 0

def process_rlc_logs(folder_path):
    """ Process all RLC log files in the given folder and compute average metrics per UE count. """
    metrics = {"queue_delay": [], "buffer_size": [], "mac_credits": []}
    
    all_files = sorted([f for f in os.listdir(folder_path) if f.endswith(".log")])

    for file_name in all_files:
        file_path = os.path.join(folder_path, file_name)

        queue_delays = []
        buffer_sizes = []
        mac_credits = []

        with open(file_path, "r") as file:
            lines = file.readlines()

        # Iterate through lines, searching for metric blocks
        for i in range(len(lines)):
            line = lines[i].strip()

            if line == "RLC Stats":
                # Extract MAC credits
                if "MAC credits:" in lines[i + 1]:
                    mac_credits.append(int(lines[i + 1].split(":")[1].strip().split()[0]))

                # Extract Queue Size
                if "Queue size:" in lines[i + 2]:
                    buffer_sizes.append(int(lines[i + 2].split(":")[1].strip().split()[0]))

                # Extract Queue Delay
                if "Queue delay:" in lines[i + 3]:
                    queue_delays.append(int(lines[i + 3].split(":")[1].strip().split()[0]))

        # Store averages per UE log
        if queue_delays and buffer_sizes and mac_credits:
            metrics["queue_delay"].append(np.mean(queue_delays))
            metrics["buffer_size"].append(np.mean(buffer_sizes))
            metrics["mac_credits"].append(np.mean(mac_credits))

    # Compute final averages across all UEs in this folder
    if metrics["queue_delay"]:
        metrics["queue_delay"] = np.mean(metrics["queue_delay"])
        metrics["buffer_size"] = np.mean(metrics["buffer_size"])
        metrics["mac_credits"] = np.mean(metrics["mac_credits"])
    else:
        metrics["queue_delay"], metrics["buffer_size"], metrics["mac_credits"] = 0, 0, 0

    return metrics

def save(filename, save_folder):
    os.makedirs(save_folder, exist_ok=True)  # Ensure the save folder exists
    plt.savefig(os.path.join(save_folder, f"{filename}.png"), dpi=300)  # Save the plot
    print(f"Saved combined metrics plot to {save_folder}/{filename}.png")

def plot_buffer_size_mac_credits(save_folder=None):
    plt.figure(figsize=(10,6))
    plt.plot(ue_counts, avg_buffer_sizes_base, marker='s', linestyle='-', label="DualPi2 AQM - Avg Buffer Size", color='b')
    plt.plot(ue_counts, avg_mac_credits_base, marker='s', linestyle='-', label="DualPi2 AQM - Avg MAC Credits", color='g')

    plt.plot(ue_counts, avg_buffer_sizes_other, marker='d', linestyle='-', label="No-AQM - Avg Buffer Size", color='y')
    plt.plot(ue_counts, avg_mac_credits_other, marker='d', linestyle='-', label="No-AQM - Avg MAC Credits", color='purple')

    plt.xlabel("Number of UEs")
    plt.ylabel("Bytes")
    plt.title("RLC Metrics vs. Number of UEs")
    plt.legend()
    plt.grid()
    plt.xticks(ue_counts)

    save("bufferSizeMacCredits", save_folder)
    plt.show()

def plot_marks_drops(save_folder=None):
    # Values extracted from the logs
    avg_marks_base = [2504, 1760, 1327.85, 1141]
    avg_drops_base = [272, 185.2, 176.14, 113.8]
    avg_drops_other = [336, 230.6, 128.3, 105.8]

    plt.figure(figsize=(10,6))
    plt.plot(ue_counts, avg_drops_base, marker='s', linestyle='-', label="DualPi2 AQM - Drops", color='b')
    plt.plot(ue_counts, avg_marks_base, marker='s', linestyle='-', label="DualPi2 AQM - Marks", color='purple')
    plt.plot(ue_counts, avg_drops_other, marker='s', linestyle='-', label="No-AQM - Drops", color='g')

    plt.xlabel("Number of UEs")
    plt.ylabel("Packets")
    plt.title("RLC Avg. Drops/Marks vs. Number of UEs")
    plt.legend()
    plt.grid()
    plt.xticks(ue_counts)

    save("dropsMarks", save_folder)
    plt.show()

def plot_throughput(save_folder=None):
    plt.figure(figsize=(10, 6))
    plt.plot(ue_counts, aqm_avg_throughputs, marker='o', linestyle='-', color='b', label="DualPi2 AQM")
    plt.plot(ue_counts, no_aqm_avg_throughputs, marker='o', linestyle='-', color='green', label="No-AQM")
    plt.xlabel("Number of UEs")
    plt.ylabel("Throughput (Mbps)")
    plt.title("Average Downlink Throughput vs. Number of UEs")
    plt.legend()
    plt.grid()
    plt.xticks(ue_counts)

    save("throughput", save_folder)
    plt.show()

def plot_queue_delay(save_folder=None):
    plt.figure(figsize=(10,6))
    
    # Offset the bars to avoid overlap for both datasets
    width = 0.3  # width of the bars
    plt.bar([x - width/2 for x in ue_counts], avg_queue_delays_base, width=width, color='yellow', edgecolor="black", alpha=0.7, linewidth=1, label="DualPi2 AQM")
    plt.bar([x + width/2 for x in ue_counts], avg_queue_delays_other, width=width, color='purple', edgecolor="black", alpha=0.7, linewidth=1, label="No-AQM")

    # Add value labels on top of bars
    for i, bar in enumerate(plt.bar([x - width/2 for x in ue_counts], avg_queue_delays_base, width=width, color='yellow', alpha=0.7)):
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2, height, f'{height:.2f}', ha='center', va='bottom', fontsize=10, fontweight='bold')

    for i, bar in enumerate(plt.bar([x + width/2 for x in ue_counts], avg_queue_delays_other, width=width, color='purple', alpha=0.7)):
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2, height, f'{height:.2f}', ha='center', va='bottom', fontsize=10, fontweight='bold')

    plt.xlabel("Number of UEs")
    plt.ylabel("Avg Queue Delay (ms)")
    plt.title("Avg Queue Delay vs. Number of UEs")
    plt.legend()
    plt.grid()
    plt.xticks(ue_counts)

    save("queueDelay", save_folder)
    plt.show()

if __name__ == '__main__':
    save_folder = "plots"
    base_folder_aqm = "../results/aqm"
    base_folder_no_aqm = "../results/no-aqm"

    # Process AQM metrics
    for ue in ue_counts:
        folder_path = os.path.join(base_folder_aqm, f"{ue}-ue")
        if os.path.exists(folder_path):
            metrics = process_rlc_logs(folder_path)
            avg_queue_delays_base.append(metrics["queue_delay"])
            avg_buffer_sizes_base.append(metrics["buffer_size"])
            avg_mac_credits_base.append(metrics["mac_credits"])
            
            filename = os.path.join(folder_path, f"default-{ue}")
            aqm_avg_throughputs.append(extract_downlink_throughput(filename))

        else:
            print(f"Warning: Folder {folder_path} not found!")
            avg_queue_delays_base.append(0)
            avg_buffer_sizes_base.append(0)
            avg_mac_credits_base.append(0)
    
    # Process no-AQM metrics
    if base_folder_no_aqm:
        for ue in ue_counts:
            folder_path = os.path.join(base_folder_no_aqm, f"{ue}-ue")
            if os.path.exists(folder_path):
                metrics = process_rlc_logs(folder_path)
                avg_queue_delays_other.append(metrics["queue_delay"])
                avg_buffer_sizes_other.append(metrics["buffer_size"])
                avg_mac_credits_other.append(metrics["mac_credits"])

                filename = os.path.join(folder_path, f"default-{ue}")
                no_aqm_avg_throughputs.append(extract_downlink_throughput(filename))

            else:
                print(f"Warning: Folder {folder_path} not found!")
                avg_queue_delays_other.append(0)
                avg_buffer_sizes_other.append(0)
                avg_mac_credits_other.append(0)
    
    plot_buffer_size_mac_credits(save_folder=save_folder)
    plot_marks_drops(save_folder=save_folder)
    plot_queue_delay(save_folder=save_folder)
    plot_throughput(save_folder=save_folder)