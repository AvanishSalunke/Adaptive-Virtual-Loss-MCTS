#!/usr/bin/env bash
# Prints machine info (date, OS, CPU, logical CPUs, RAM, compiler, git commit)
# as key=value lines, so a benchmark result can be tied to the machine and
# code version that produced it.
echo "date=$(date -Iseconds)"
echo "kernel=$(uname -sr)"
echo "cpu=$(lscpu | sed -n 's/^Model name:[[:space:]]*//p')"
echo "logical_cpus=$(nproc --all)"
echo "ram_mb=$(free -m | awk '/Mem:/{print $2}')"
echo "compiler=$(g++ --version | head -1)"
echo "git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo none)"
