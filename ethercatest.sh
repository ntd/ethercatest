#!/bin/bash
# Usage:
#   ethercatest.sh STACK [PERIOD]
# where STACK can be soem, gatorcat or igh.

die() {
    echo "$1" >&2
    exit 1
}

stress=$(command -v stress)
test -x "$stress" || die 'You need to install `stress`'
test -n "$1" || die 'You need to specify an EtherCAT stack (soem, gatorcat or igh)'

stack=$1
binary="./zig-out/bin/ethercatest-$stack"
test -x "$binary" || die "'$stack' is not a valid EtherCAT stack"

test -z "$2" && period=1000 || period=$2


set -o pipefail


run_test() {
    local niceness=$1
    local period=$2
    if ! nice -n$niceness $binary -q $period 2>&1 | awk '/^Iteration time/ { print $5 ", " $7 ", " $9 ", " $11 }'; then
        die "** ERROR DURING THE RUN: do you have root privileges? The interface is up?"
        exit 1
    fi
}

run_tests() {
    local busy=$1
    local period=$2
    # Warm up: throw the first run
    run_test -20 $period > /dev/null
    for niceness in $(seq -20 2 0); do
        printf "\"$stack\", $busy, $niceness, $period, "
        run_test $niceness $period
    done
}


printf "Stack, Busy, Niceness, Period, Min time, Max time, Total time, Errors\n"
run_tests 0 $period


stress_pid=
kill_stress() {
    test -n "$stress_pid" && kill $stress_pid
    exit
}
trap kill_stress INT TERM EXIT

( "$stress" --quiet --cpu 256 ) &
stress_pid=$!


# Repeat the same tests with busy CPU
run_tests 1 $period
