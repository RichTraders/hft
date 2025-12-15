# HFT Engine Systemd Service Guide

This guide explains how to register and manage the HFT engine as a systemd service.

## Table of Contents
- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Service Installation](#service-installation)
- [Service Control](#service-control)
- [Log Viewing](#log-viewing)
- [Troubleshooting](#troubleshooting)

---

## Overview

The HFT engine must run in the `iso.slice` cgroup for CPU isolation. Registering it as a systemd service provides the following benefits:

- ✅ Automatic start on boot
- ✅ Automatic restart on process crash
- ✅ Unified log management through systemd
- ✅ Standardized service control methods
- ✅ Automatic application of CPU isolation settings

---

## Prerequisites

### 1. Verify iso.slice Configuration

The `/etc/systemd/system/iso.slice` file must exist with the following content:

```ini
[Unit]
Description=Top-level isolated slice

[Slice]
AllowedCPUs=0-4
AllowedMemoryNodes=0
Delegate=cpu cpuset memory pids io
```

Verification:
```bash
cat /etc/systemd/system/iso.slice
```

If it doesn't exist, create it:
```bash
sudo tee /etc/systemd/system/iso.slice > /dev/null <<EOF
[Unit]
Description=Top-level isolated slice

[Slice]
AllowedCPUs=0-4
AllowedMemoryNodes=0
Delegate=cpu cpuset memory pids io
EOF

sudo systemctl daemon-reload
```

### 2. Build HFT Executable

Build in Release mode:
```bash
PROJECT_MAIN="Your HFT engine project"
cd {PROJECT_MAIN}
cmake --build cmake-build-release-clang --target HFT
```

---

## Service Installation

### Step 1: Copy Service File

Copy the `util/hft-engine.service` file to the systemd directory:

```bash
sudo cp {PROJECT_MAIN}/util/hft-engine.service /etc/systemd/system/
```

### Step 2: Verify Permissions

Verify the service file permissions:

```bash
sudo chmod 644 /etc/systemd/system/hft-engine.service
```

### Step 3: Reload systemd

Reload systemd to recognize the new service:

```bash
sudo systemctl daemon-reload
```

### Step 4: Enable Service (Optional)

If you want automatic start on boot:

```bash
sudo systemctl enable hft-engine.service
```

---

## Service Control

### Start Service

```bash
sudo systemctl start hft-engine.service
```

### Stop Service

```bash
sudo systemctl stop hft-engine.service
```

### Restart Service

```bash
sudo systemctl restart hft-engine.service
```

### Check Service Status

```bash
systemctl status hft-engine.service
```

Example output:
```
● hft-engine.service - HFT engine
     Loaded: loaded (/etc/systemd/system/hft-engine.service; enabled; vendor preset: enabled)
     Active: active (running) since Sun 2025-11-17 10:00:00 KST; 1h ago
   Main PID: 12345 (HFT)
      Tasks: 5 (limit: 38363)
     Memory: 512.0M
        CPU: 30min 15.234s
     CGroup: /iso.slice/hft-engine.service
             └─12345 /home/neworo/CLionProjects/hft/cmake-build-release-clang/HFT
```

### Disable Service (Disable Automatic Start)

```bash
sudo systemctl disable hft-engine.service
```

---

## Log Viewing

### View Real-time Logs

```bash
journalctl -u hft-engine.service -f
```

### View Recent Logs

```bash
# Last 100 lines
journalctl -u hft-engine.service -n 100

# Last 1 hour
journalctl -u hft-engine.service --since "1 hour ago"

# Today's logs only
journalctl -u hft-engine.service --since today
```

### View Logs for Specific Time Period

```bash
journalctl -u hft-engine.service --since "2025-11-17 09:00:00" --until "2025-11-17 10:00:00"
```

### Search Logs

```bash
# View errors only
journalctl -u hft-engine.service -p err

# Search for specific keyword
journalctl -u hft-engine.service | grep "ERROR"
```

---

## Grant Regular User Permissions (Optional)

To allow regular users to control the service without sudo password:

### 1. Create sudoers File

```bash
sudo visudo -f /etc/sudoers.d/hft-service
```

### 2. Add the Following Content

```
neworo ALL=(root) NOPASSWD: /usr/bin/systemctl start hft-engine.service
neworo ALL=(root) NOPASSWD: /usr/bin/systemctl stop hft-engine.service
neworo ALL=(root) NOPASSWD: /usr/bin/systemctl restart hft-engine.service
neworo ALL=(root) NOPASSWD: /usr/bin/systemctl status hft-engine.service
```

### 3. Set Permissions

```bash
sudo chmod 0440 /etc/sudoers.d/hft-service
```

Now you can control the service without password:
```bash
sudo systemctl start hft-engine.service
sudo systemctl stop hft-engine.service
```

---

## Troubleshooting

### Service Fails to Start

1. **Check Service Status**
   ```bash
   systemctl status hft-engine.service
   ```

2. **Check Detailed Logs**
   ```bash
   journalctl -u hft-engine.service -n 50 --no-pager
   ```

3. **Verify Executable**
   ```bash
   ls -l {PROJECT_MAIN}/hft/cmake-build-release-clang/HFT
   ```

4. **Test Manual Execution**
   ```bash
   sudo systemd-run --scope --slice=iso.slice {PROJECT_MAIN}/hft/cmake-build-release-clang/HFT
   ```

### CPU Isolation Not Working

1. **Check Process cgroup**
   ```bash
   # After starting service
   PID=$(systemctl show -p MainPID hft-engine.service | cut -d= -f2)
   cat /proc/$PID/cgroup
   ```

   Output should contain `iso.slice`:
   ```
   0::/iso.slice/hft-engine.service
   ```

2. **Check CPU Affinity**
   ```bash
   PID=$(systemctl show -p MainPID hft-engine.service | cut -d= -f2)
   taskset -cp $PID
   ```

   Output:
   ```
   pid 12345's current affinity list: 0-4
   ```

3. **Check CPU for Each Thread**
   ```bash
   PID=$(systemctl show -p MainPID hft-engine.service | cut -d= -f2)
   ls /proc/$PID/task/ | xargs -I {} sh -c 'echo "TID: {}"; cat /proc/$PID/task/{}/comm; taskset -cp {}'
   ```

### Service Keeps Restarting

1. **Check Crash Logs**
   ```bash
   journalctl -u hft-engine.service -p err -n 100
   ```

2. **Check Core Dumps**
   ```bash
   coredumpctl list
   coredumpctl info
   ```

3. **Check Configuration File**
   ```bash
   cat /home/neworo/CLionProjects/hft/resources/config.ini
   ```

### Cannot Find iso.slice

```bash
# Check iso.slice status
systemctl status iso.slice

# If iso.slice doesn't exist, create it
sudo tee /etc/systemd/system/iso.slice > /dev/null <<EOF
[Unit]
Description=Top-level isolated slice

[Slice]
AllowedCPUs=0-4
AllowedMemoryNodes=0
Delegate=cpu cpuset memory pids io
EOF

sudo systemctl daemon-reload
```

---

## CPU Isolation Management

The HFT service uses dynamic CPU isolation to ensure dedicated CPU resources for trading operations.

### How It Works

1. **During HFT Service Start**:
   - The `manage_cpu_isolation.sh` script automatically reads the `AllowedCPUs` setting from `/etc/systemd/system/iso.slice`
   - It calculates which CPUs should be isolated for HFT (e.g., CPU 0-4)
   - It restricts all other system slices (`init.scope`, `system.slice`, `user.slice`, `machine.slice`) to use only the remaining CPUs (e.g., CPU 5-11)

2. **During HFT Service Stop**:
   - The script automatically restores all slices to use all available CPUs (e.g., CPU 0-11)
   - No manual intervention required

### Changing CPU Allocation

To change which CPUs are isolated for HFT, you only need to modify **one file**:

```bash
sudo nano /etc/systemd/system/iso.slice
```

Change the `AllowedCPUs` line:
```ini
[Slice]
AllowedCPUs=0-4  # Change this to your desired CPU range (e.g., 0-7)
```

Then reload systemd:
```bash
sudo systemctl daemon-reload
```

The `manage_cpu_isolation.sh` script will automatically adapt to the new configuration - no other changes needed!

### Manual CPU Isolation Control

You can manually trigger CPU isolation without starting the HFT service:

```bash
# Restrict other slices to non-isolated CPUs
sudo /home/neworo/CLionProjects/hft/util/manage_cpu_isolation.sh start

# Restore all slices to use all CPUs
sudo /home/neworo/CLionProjects/hft/util/manage_cpu_isolation.sh stop
```

---

## Modifying Service File

To change service behavior:

### 1. Edit Service File

```bash
sudo nano /etc/systemd/system/hft-engine.service
```

### 2. Reload After Changes

```bash
sudo systemctl daemon-reload
```

### 3. Restart Service

```bash
sudo systemctl restart hft-engine.service
```

### Key Configuration Options

- **WorkingDirectory**: HFT execution directory (where config.ini is located)
- **ExecStart**: Path to executable file
- **ExecStartPre**: Script to run before starting (CPU isolation setup)
- **ExecStopPost**: Script to run after stopping (CPU isolation cleanup)
- **Restart**: Restart policy (on-failure, always, no)
- **RestartSec**: Wait time before restart
- **Slice**: cgroup slice name

---

## Reference Materials

- **systemd Manual**: `man systemd.service`
- **journalctl Manual**: `man journalctl`
- **systemctl Manual**: `man systemctl`
- **cgroup Configuration**: `man systemd.resource-control`

---

## Slack Notification Setup (Optional)

You can use slack_notifier to receive Slack notifications for HFT engine status changes.

### Using Supervisor

1. **Set Slack Webhook URL**
   ```bash
   export SLACK_WEBHOOK_URL="https://hooks.slack.com/services/YOUR/WEBHOOK/URL"
   ```

2. **Configure Supervisor**
   ```bash
   sudo cp util/slack_notifier.conf /etc/supervisor/conf.d/
   sudo cp util/slack_notifier.py /opt/supervisor/listeners/
   ```

3. **Modify Configuration File**
   ```bash
   sudo nano /etc/supervisor/conf.d/slack_notifier.conf
   # Enter actual Webhook URL in environment line
   ```

4. **Restart Supervisor**
   ```bash
   sudo supervisorctl reread
   sudo supervisorctl update
   sudo supervisorctl status slack_notifier
   ```

### Using Systemd

1. **Install Dependencies**
   ```bash
   pip install pydbus PyGObject
   ```

2. **Copy Service File**
   ```bash
   sudo cp util/slack-notifier.service /etc/systemd/system/
   ```

3. **Configure Webhook URL**
   ```bash
   sudo nano /etc/systemd/system/slack-notifier.service
   # Enter actual Webhook URL in Environment line
   ```

4. **Start Service**
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable slack-notifier.service
   sudo systemctl start slack-notifier.service
   ```

5. **Check Status**
   ```bash
   systemctl status slack-notifier.service
   journalctl -u slack-notifier.service -f
   ```

### Manual Execution (For Testing)

```bash
# Supervisor mode
export SLACK_WEBHOOK_URL="https://hooks.slack.com/services/YOUR/WEBHOOK/URL"
python3 util/slack_notifier.py supervisor

# Systemd mode
export SLACK_WEBHOOK_URL="https://hooks.slack.com/services/YOUR/WEBHOOK/URL"
python3 util/slack_notifier.py systemd --service hft-engine.service
```

### Notification Examples

- **Service Start**: ✅ Systemd Monitor Started
- **Service Stop**: ⏹️ Service State Changed (active → inactive)
- **Service Failure**: 💥 Service Failed (includes exit code)
- **Restart**: 🔄 Service State Changed (activating → active)

---

## Support

If you have service-related issues, please provide the following information:

```bash
# System information
uname -a
cat /etc/os-release

# Service status
systemctl status hft-engine.service

# Recent logs
journalctl -u hft-engine.service -n 50 --no-pager

# cgroup information
cat /proc/$(systemctl show -p MainPID hft-engine.service | cut -d= -f2)/cgroup
```

If cpus do not work after stop cpu isolation, try cpu hotplug

```bash
#In this case, hotplug for cpu 1 to 4.
for i in {1..4}; do echo 0 | sudo tee /sys/devices/system/cpu/cpu$i/online; done
sleep 1
for i in {1..4}; do echo 1 | sudo tee /sys/devices/system/cpu/cpu$i/online; done
```