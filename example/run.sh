# Description: This script is used to run the sample files.
# open the script debug
set -x
# compile case files
g++ -g -o ./case/sleep_off_cpu ./case/sleep_off_cpu.cpp -lrt
g++ -g -o ./case/read_off_cpu ./case/read_off_cpu.cpp -lrt
g++ -g -o ./case/on_cpu_hotspot ./case/on_cpu_hotspot.cpp

# compile sample files
g++ -o pmu_hotspot pmu_hotspot.cpp -I ../output/include -L ../output/lib -lkperf -lsym

# import kperf library
export LD_LIBRARY_PATH=../output/lib:$LD_LIBRARY_PATH

# check user input
if [ $# -eq 0 ]; then
    echo "Usage: $0 {sleep|read|oncpu|all}"
    exit 1
fi

# run sample files based on user input
case "$1" in
    sleep)
        ./pmu_hotspot ./case/sleep_off_cpu
        ;;
    read)
        ./pmu_hotspot ./case/read_off_cpu
        ;;
    oncpu)
        ./pmu_hotspot ./case/on_cpu_hotspot
        ;;
    all)
        ./pmu_hotspot ./case/sleep_off_cpu
        ./pmu_hotspot ./case/read_off_cpu
        ./pmu_hotspot ./case/on_cpu_hotspot
        ;;
    *)
        echo "Invalid option: $1"
        echo "Usage: $0 {sleep|read|oncpu|all}"
        exit 1
        ;;
esac