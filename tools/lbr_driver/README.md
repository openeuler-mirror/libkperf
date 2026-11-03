# x86 LBR Collection Tool
`simple-lbr-trace` is an x86 (Intel) Linux kernel module that collects branch information (LBR) using `sched_switch` and `hrtimer` writes samples into a per-CPU ring buffer. The data is exposed to user space via `/dev/simple-lbr-trace`.

## Requirements
1. Kernel build directory must exist:
   - `/lib/modules/$(uname -r)/build`

2. Root privileges are required to load kernel modules.
   - In containers, run with `--privileged`.

3. Supported environments:
   - Linux kernel **5.4** only.
   - The CPU must support the required PMU features (Arch LBR).


Tested CPU:
- **Emerald Rapids** (Architectural LBR)

## Build

```shell
cd tools/lbr_driver
make
```

## Install
```shell
sudo make install
sudo depmod -a
sudo modprobe simple-lbr-trace
```
Or, in a debug environment:
```shell
sudo insmod simple-lbr-trace.ko
```

## Check
Confirm the device exists:
```shell
test -c /dev/simple-lbr-trace && echo "OK"
```

Check `dmesg`:
```shell
dmesg | tail -n 200 | grep -i trace
```
You should see output similar to:
```shell
[23533.198653] Initialized: device=/dev/simple-lbr-trace, sample_period_us=500
```

## Uninstall
```shell
sudo modprobe -r simple-lbr-trace
```
Or, in a debug environment:
```shell
sudo rmmod simple-lbr-trace
```

Check `dmesg`, you can see the exit infomation:
```shell
[23629.530629] simple-trace-lbr: Exited
```

## Usage
Build the libkperf tool first:
```shell
bash build.sh
```

The LBR collection demo is documented in: libkperf/docs/Details_Usage/采集BRBE数据

Current limitations:
- A PID must be specified for collection.
- Only collect pmu 'cycles' event.
- The tsc time in output data is `rdtsc` time, which is different from the ts in PMU collection.

To change the sampling period, specify the `sample_period` value when install the driver like:
```shell
sudo insmod simple-lbr-trace.ko sample_period=200
```

To Configuring the branch event, modify the `ctl` in `lbr_enable_cpu` function.
