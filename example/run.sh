#!/bin/bash
# Description: This script is used to run the sample files.
# Enable script debugging
set -x

# Default values
PYTHON=false
GO=false

# Parse user input arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 {sleep|read|oncpu|all} [python=true]"
    exit 1
fi

# Extract the first argument (case type)
CASE_TYPE=$1
shift

# Parse optional arguments (e.g., python=true)
for arg in "$@"; do
    if [[ "$arg" == "python=true" ]]; then
        PYTHON=true
    fi
    if [[ "$arg" == "go=true" ]]; then
        GO=true
    fi
done

# Build the kperf library and compile case files
cd ../
bash build.sh python=$PYTHON go=$GO
cd example

# Compile case files
g++ -g -o ./case/sleep_off_cpu ./case/sleep_off_cpu.cpp -lrt
g++ -g -o ./case/read_off_cpu ./case/read_off_cpu.cpp -lrt
g++ -g -o ./case/on_cpu_hotspot ./case/on_cpu_hotspot.cpp

# Compile sample files
g++ -o pmu_hotspot pmu_hotspot.cpp -I ../output/include -L ../output/lib -lkperf -lsym

# Import kperf library
export LD_LIBRARY_PATH=../output/lib:$LD_LIBRARY_PATH

# Function to run a case with C or Python
run_case() {
    local case_file=$1
    if [[ "$PYTHON" == true ]]; then
        python3 pmu_hotspot.py 1 1 1 "$case_file"
    elif [[ "$GO" == true ]]; then
        export GO111MODULE=off
        export GOPATH=$(realpath ../go)
        if [ ! -d  ../go/src/main ]; then
             mkdir -p ../go/src/main 
        fi
        cp pmu_hotspot.go ../go/src/main/
        mv ../go/src/main/pmu_hotspot.go ../go/src/main/main.go
        pushd ../go/src/main
        go build main.go
        popd
        cp ../go/src/main/main ./pmu_hotspot_of_go
        ./pmu_hotspot_of_go 1 1 1 ""$case_file""
    else
        ./pmu_hotspot 1 1 1 "$case_file"
    fi
}

# Run cases based on user input
case "$CASE_TYPE" in
    sleep)
        run_case ./case/sleep_off_cpu
        ;;
    read)
        run_case ./case/read_off_cpu
        ;;
    oncpu)
        run_case ./case/on_cpu_hotspot
        ;;
    all)
        run_case ./case/sleep_off_cpu
        run_case ./case/read_off_cpu
        run_case ./case/on_cpu_hotspot
        ;;
    *)
        echo "Invalid option: $CASE_TYPE"
        echo "Usage: $0 {sleep|read|oncpu|all} [python=true]"
        exit 1
        ;;
esac