# x86 LBR Collection Tool
`simple-pebs` is an x86 (Intel) Linux kernel module that collects branch information (LBR) using PEBS + LBR and writes samples into a per-CPU output buffer. The data is exposed to user space via `/dev/simple-pebs`.

## Requirements
1. Kernel build directory must exist:
   - `/lib/modules/$(uname -r)/build`

2. Root privileges are required to load kernel modules.
   - In containers, run with `--privileged`.

3. Supported environments:
   - Linux kernel **5.4** only.
   - The CPU must support the required PMU features (PEBS / LBR).


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
sudo modprobe simple-pebs
```
Or, in a debug environment:
```shell
sudo insmod simple-pebs.ko
```

## Check
Confirm the device exists:
```shell
test -c /dev/simple-pebs && echo "OK"
```

Check `dmesg`:
```shell
dmesg | tail -n 200 | grep -i pebs
```
You should see output similar to:
```shell
[23533.195839] simple_pebs: Adaptive PEBS (v4) detected
[23533.198653] simple_pebs: Initialized: device=/dev/simple-pebs
```

## Uninstall
```shell
sudo modprobe -r simple-pebs
```
Or, in a debug environment:
```shell
sudo rmmod simple-pebs
```

Check `dmesg`, you can see the exit infomation:
```shell
[23629.530629] simple_pebs: Exited
```

## Usage
Build the libkperf tool first:
```shell
bash build.sh
```

The LBR collection demo is documented in: libkperf/docs/Details_Usage/采集BRBE数据

Current limitations:
- PID and CPU filtering is not supported.
- Only collect pmu 'cycles' event.
- The tsc time in output data is `rdtsc` time, which is different from the ts in PMU collection.

To change the sampling period, modify the `PERIOD` value in the driver source code and rebuild the module.

To Configuring the branch event, modify the `ctl` in `setup_arch_lbr` function.
